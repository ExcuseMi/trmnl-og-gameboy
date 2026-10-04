/* See pad.h. NimBLE central: scan (HID service 0x1812, gamepad appearance or a bonded address), connect, pair
 * (Just Works, bond in NVS), enable notifications of the HID service, parse the reports with xbox_report.c.
 * Log lines over USB: pad scanning / link / connected / report buttons=.. / disconnected. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "pad.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_bt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "xbox_report.h"

#define UUID_HID 0x1812
#define UUID_CCCD 0x2902
#define APPEARANCE_GAMEPAD 0x03c4
#define MAX_CCCD 4
#define SKIP_US 30000000 /* a device that was no usable pad is ignored this long, so it cannot block the scan */

void ble_store_config_init(void);

static uint8_t own_addr_type;
static volatile uint8_t g_buttons;
static volatile uint16_t g_raw; /* Xbox button word (xbox_report.h XBOX_*), for keys the Game Boy does not have */
static volatile bool g_connected;
static uint16_t hid_start, hid_end;
static uint16_t cccd[MAX_CCCD];
static int n_cccd, i_cccd;
#define MAX_CHR 16
static uint16_t rd[MAX_CHR]; /* value handles of the readable characteristics of the HID service */
static int n_rd, i_rd;
static ble_addr_t peer, skip;
static int64_t skip_until;
static const char *volatile g_state = "off";
static volatile uint32_t n_adv, n_pad;
static volatile int g_err;
static volatile uint32_t n_link, n_rescan;
static volatile uint32_t n_notif, n_reset;
static volatile uint32_t n_rx; /* GATT callbacks run (reads, discovery, writes): each is at least one packet received */
static volatile uint32_t n_ev;  /* GAP events handled: stands still if the host task hangs */
static volatile int g_evt = -1; /* type of the last GAP event */

/* The host task never prints: a blocking console write there holds up the Bluetooth host (seen on hardware as a
 * receive pool that ran empty). Lines go into a small ring, the status call (emulator task) prints them. */
#define EV_N 16
static char evlog[EV_N][80];
static volatile uint8_t ev_w, ev_r;

static void evf(const char *fmt, ...)
{
    char *line = evlog[ev_w % EV_N];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof evlog[0], fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof evlog[0]) n = sizeof evlog[0] - 1;
    if (n > 0 && line[n - 1] == '\n') line[n - 1] = 0;
    ev_w++;
}
static char g_peer[24] = "-"; /* end of the address and RSSI of the device last connected to */
static volatile unsigned g_len; /* length of the last notification */
static volatile int g_disc; /* reason of the last disconnect */

static int gap_event(struct ble_gap_event *ev, void *arg);

/* Linker wraps (CMakeLists.txt): the transport's receive buffer allocation and the host's processing of a received
 * packet, to see on hardware where received packets stay. */
struct os_mbuf *__real_ble_transport_alloc_acl_from_ll(void);
int __real_ble_hs_hci_evt_acl_process(struct os_mbuf *om);
static struct os_mempool *volatile acl_pool;
static volatile uint32_t n_alloc, n_alloc_fail, n_proc, n_proc_ok, n_proc_again, n_proc_err;
static volatile int last_rc;
static volatile uint8_t last_head[8];
static volatile uint16_t last_len;

struct os_mbuf *__wrap_ble_transport_alloc_acl_from_ll(void)
{
    struct os_mbuf *om = __real_ble_transport_alloc_acl_from_ll();
    if (om) {
        n_alloc++;
        acl_pool = om->om_omp->omp_pool;
    } else {
        n_alloc_fail++;
    }
    return om;
}

int __wrap_ble_hs_hci_evt_acl_process(struct os_mbuf *om)
{
    uint8_t h[8] = { 0 };
    uint16_t len = OS_MBUF_PKTLEN(om);
    os_mbuf_copydata(om, 0, len < sizeof h ? len : (int)sizeof h, h);
    int rc = __real_ble_hs_hci_evt_acl_process(om);
    n_proc++;
    if (rc == 0) n_proc_ok++;
    else if (rc == BLE_HS_EAGAIN) n_proc_again++;
    else n_proc_err++;
    last_rc = rc;
    last_len = len;
    memcpy((void *)last_head, h, sizeof h);
    return rc;
}

static void scan_start(void)
{
    struct ble_gap_disc_params p = { .itvl = 0x60, .window = 0x30, .passive = 0 } /* active: the name is in the scan response */;
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    g_state = "scan";
    if (rc && rc != BLE_HS_EALREADY) {
        g_err = rc;
        evf("pad error=scan rc=%d\n", rc);
    }
}

static bool bonded(const ble_addr_t *a)
{
    ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, MYNEWT_VAL(BLE_STORE_MAX_BONDS))) return false;
    for (int i = 0; i < n; i++)
        if (!ble_addr_cmp(&peers[i], a)) return true;
    return false;
}

/* Not a usable pad: drop the link and look elsewhere for a while. */
static void give_up(uint16_t conn, const char *why, int rc)
{
    evf("pad error=%s rc=%d\n", why, rc);
    g_err = rc;
    skip = peer;
    skip_until = esp_timer_get_time() + SKIP_US;
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

static void subscribe_next(uint16_t conn);

static int on_sub(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    n_rx++;
    (void)attr;
    (void)arg;
    if (err->status) {
        g_err = err->status;
        evf("pad error=subscribe handle=%u rc=%d\n", cccd[i_cccd], err->status);
    }
    i_cccd++;
    subscribe_next(conn);
    return 0;
}

static void subscribe_next(uint16_t conn)
{
    static const uint8_t on[2] = { 1, 0 };
    if (i_cccd < n_cccd) {
        int rc = ble_gattc_write_flat(conn, cccd[i_cccd], on, sizeof on, on_sub, NULL);
        if (rc) give_up(conn, "subscribe", rc);
        return;
    }
    g_connected = true;
    g_state = "on";
    evf("pad connected reports=%d\n", n_cccd);
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val_handle,
                  const struct ble_gatt_dsc *dsc, void *arg)
{
    n_rx++;
    (void)chr_val_handle;
    (void)arg;
    if (err->status == 0) {
        if (ble_uuid_u16(&dsc->uuid.u) == UUID_CCCD && n_cccd < MAX_CCCD) cccd[n_cccd++] = dsc->handle;
        return 0;
    }
    if (err->status == BLE_HS_EDONE && n_cccd) {
        i_cccd = 0;
        subscribe_next(conn);
    } else {
        give_up(conn, "no input report", err->status);
    }
    return 0;
}

/* The Xbox pad sends no reports until the readable characteristics of its HID service (report map, HID information,
 * reports) were read once: seen on hardware (subscribed, zero notifications) and in asukiaaa's
 * XboxSeriesXControllerESP32 ("Reading value is required for subscribe"). So: read them all, then subscribe. */
static void read_next(uint16_t conn);

static int on_read(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    n_rx++;
    (void)attr;
    (void)arg;
    if (err->status == 0) return 0; /* a part of a long value; BLE_HS_EDONE follows */
    if (err->status != BLE_HS_EDONE) evf("pad read handle=%u rc=%d\n", rd[i_rd], err->status);
    i_rd++;
    read_next(conn);
    return 0;
}

static void read_next(uint16_t conn)
{
    while (i_rd < n_rd) {
        if (!ble_gattc_read_long(conn, rd[i_rd], 0, on_read, NULL)) return;
        i_rd++;
    }
    n_cccd = 0;
    int rc = ble_gattc_disc_all_dscs(conn, hid_start, hid_end, on_dsc, NULL);
    if (rc) give_up(conn, "descriptors", rc);
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    n_rx++;
    (void)arg;
    if (err->status == 0) {
        if ((chr->properties & BLE_GATT_CHR_PROP_READ) && n_rd < MAX_CHR) rd[n_rd++] = chr->val_handle;
        return 0;
    }
    if (err->status == BLE_HS_EDONE) {
        evf("pad hid readable=%d\n", n_rd);
        i_rd = 0;
        read_next(conn);
    } else {
        give_up(conn, "characteristics", err->status);
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    n_rx++;
    (void)arg;
    if (err->status == 0) {
        hid_start = svc->start_handle;
        hid_end = svc->end_handle;
        return 0;
    }
    int rc = err->status;
    if (rc == BLE_HS_EDONE && hid_end) {
        n_rd = 0;
        rc = ble_gattc_disc_all_chrs(conn, hid_start, hid_end, on_chr, NULL);
        if (!rc) return 0;
    }
    give_up(conn, "no hid service", rc);
    return 0;
}

static void forget(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    if (!ble_gap_conn_find(conn, &d)) ble_store_util_delete_peer(&d.peer_id_addr);
}

static bool wanted(const struct ble_gap_disc_desc *d)
{
    if (!ble_addr_cmp(&d->addr, &skip) && esp_timer_get_time() < skip_until) return false;
    if (bonded(&d->addr)) return true; /* a bonded pad may advertise directed, without data */
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data)) return false;
    /* Only gamepads: any HID device (a remote, a keyboard nearby) would take the one connection. */
    if (f.appearance_is_present && f.appearance == APPEARANCE_GAMEPAD) return true;
    if (f.name && f.name_len >= 4)
        for (int i = 0; i + 4 <= f.name_len; i++)
            if (!memcmp(f.name + i, "Xbox", 4)) return true;
    return false;
}

static int on_mtu(uint16_t conn, const struct ble_gatt_error *err, uint16_t mtu, void *arg)
{
    n_rx++;
    (void)arg;
    evf("pad mtu=%u rc=%d", mtu, err ? err->status : -1);
    int rc = ble_gattc_disc_svc_by_uuid(conn, BLE_UUID16_DECLARE(UUID_HID), on_svc, NULL);
    if (rc) give_up(conn, "discover", rc);
    return 0;
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    int rc;
    n_ev++;
    g_evt = ev->type;
    switch (ev->type) {
    case BLE_GAP_EVENT_DISC:
        n_adv++;
        if (!wanted(&ev->disc)) return 0;
        n_pad++;
        peer = ev->disc.addr;
        {
            /* which device this is: several gamepads may be in range, and only one of them is in the player's hands */
            struct ble_hs_adv_fields f;
            char name[24] = "";
            if (!ble_hs_adv_parse_fields(&f, ev->disc.data, ev->disc.length_data) && f.name && f.name_len) {
                size_t n = f.name_len < sizeof name - 1 ? f.name_len : sizeof name - 1;
                memcpy(name, f.name, n);
                name[n] = 0;
            }
            evf("pad found %02x:%02x:%02x:%02x:%02x:%02x rssi=%d bonded=%d name='%s'\n", peer.val[5], peer.val[4],
                   peer.val[3], peer.val[2], peer.val[1], peer.val[0], ev->disc.rssi, bonded(&peer), name);
            snprintf(g_peer, sizeof g_peer, "%02x%02x%02x rssi=%d", peer.val[2], peer.val[1], peer.val[0],
                     ev->disc.rssi);
        }
        ble_gap_disc_cancel();
        rc = ble_gap_connect(own_addr_type, &peer, 10000, NULL, gap_event, NULL);
        if (rc) {
            g_err = rc;
            evf("pad error=connect rc=%d\n", rc);
            scan_start();
        }
        return 0;
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status) {
            g_err = ev->connect.status;
            scan_start();
            return 0;
        }
        evf("pad link %02x:%02x:%02x:%02x:%02x:%02x\n", peer.val[5], peer.val[4], peer.val[3], peer.val[2],
               peer.val[1], peer.val[0]);
        hid_start = hid_end = 0;
        n_link++;
        {
            int8_t rssi = 0;
            ble_gap_conn_rssi(ev->connect.conn_handle, &rssi);
            evf("pad rssi=%d\n", rssi);
        }
        g_state = "pair";
        rc = ble_gap_security_initiate(ev->connect.conn_handle);
        if (rc) give_up(ev->connect.conn_handle, "security", rc);
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.status) {
            forget(ev->enc_change.conn_handle); /* e.g. the pad lost its bond: pair again next time */
            g_err = ev->enc_change.status;
            evf("pad error=pairing rc=%d\n", ev->enc_change.status);
            ble_gap_terminate(ev->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        g_state = "setup";
        /* MTU exchange first, as the host libraries that work with the Xbox pad do on every connect */
        rc = ble_gattc_exchange_mtu(ev->enc_change.conn_handle, on_mtu, NULL);
        if (rc) on_mtu(ev->enc_change.conn_handle, NULL, 0, NULL);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        forget(ev->repeat_pairing.conn_handle);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint8_t d[32];
        uint16_t n = OS_MBUF_PKTLEN(ev->notify_rx.om);
        xbox_report_t r;
        if (n > sizeof d) n = sizeof d;
        if (os_mbuf_copydata(ev->notify_rx.om, 0, n, d)) return 0;
        n_notif++;
        g_len = n;
        bool ok = xbox_parse(d, n, &r);
        /* the first reports raw, and later ones the parser refuses now and then: the format is checked on hardware */
        if (n_notif <= 4 || (!ok && n_notif % 200 == 0)) {
            char line[72];
            int o = snprintf(line, sizeof line, "pad raw handle=%u len=%u ok=%d", ev->notify_rx.attr_handle, n, ok);
            for (int i = 0; i < n && o < (int)sizeof line - 3; i++) o += snprintf(line + o, sizeof line - o, " %02x", d[i]);
            evf("%s", line);
        }
        if (!ok) return 0;
        g_raw = r.buttons;
        uint8_t b = xbox_to_gb(&r);
        if (b != g_buttons) {
            g_buttons = b;
            evf("pad report buttons=0x%02x\n", b);
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        g_connected = false;
        g_buttons = 0;
        g_raw = 0;
        g_disc = ev->disconnect.reason;
        evf("pad disconnected reason=%d\n", ev->disconnect.reason);
        scan_start();
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        scan_start();
        return 0;
    default:
        return 0;
    }
}

/* Runs in the host task twice a second: a count that stands still means the host task hangs. */
static struct ble_npl_callout beat;
static volatile uint32_t n_beat;

static void beat_cb(struct ble_npl_event *ev)
{
    (void)ev;
    n_beat++;
    ble_npl_callout_reset(&beat, ble_npl_time_ms_to_ticks32(500));
}

static void on_sync(void)
{
    static bool once;
    if (!once) {
        once = true;
        ble_npl_callout_init(&beat, nimble_port_get_dflt_eventq(), beat_cb, NULL);
        ble_npl_callout_reset(&beat, ble_npl_time_ms_to_ticks32(500));
    }
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc) {
        g_err = rc;
        evf("pad error=address rc=%d\n", rc);
        return;
    }
    /* Bonds made before the firmware connected to gamepads only may be with another HID device: drop them once. */
    nvs_handle_t h;
    if (!nvs_open("gb", NVS_READWRITE, &h)) {
        uint8_t v = 0;
        if (nvs_get_u8(h, "bondv", &v) || v != 3) { /* 3: legacy pairing */
            ble_store_clear();
            nvs_set_u8(h, "bondv", 3);
            nvs_commit(h);
            evf("pad bonds cleared\n");
        }
        nvs_close(h);
    }
    evf("pad scanning\n");
    scan_start();
}

static void on_reset(int reason) { evf("pad error=host reset reason=%d\n", reason); }

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void pad_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e) { /* first start, or foreign data from another firmware in the partition */
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (!e) e = nimble_port_init();
    if (!e) ble_att_set_preferred_mtu(255);
    if (!e) esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9); /* +9 dBm, as the working Xbox hosts */
    if (e) {
        g_err = (int)e;
        evf("pad error=init rc=%d\n", (int)e);
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; /* Just Works */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0; /* legacy pairing, as the Xbox host libraries that work use (bond, no MITM, no SC) */
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    /* The gamepad is a GATT client too and sends us requests. Without the GATT server NimBLE drops each one
     * unanswered and never frees its buffer; after 24 the receive pool is empty and no report arrives. */
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("TRMNL Game Boy");
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
}

uint8_t pad_buttons(void) { return g_buttons; }
uint16_t pad_raw_buttons(void) { return g_raw; }
bool pad_connected(void) { return g_connected; }
/* Called once a second for the status line. Also the scan watchdog: seen on hardware, the advert count stood still
 * in state "scan" after one gamepad advert (a connect attempt that never reported back), so after 5 s without an
 * advert the pending connect and the scan are cancelled and the scan starts again. */
const char *pad_state(void)
{
    static char text[128];
    static uint32_t seen;
    static int still, quiet;
    const char *st = g_state;
    if (!g_connected && !strcmp(st, "scan")) {
        if (n_adv != seen) still = quiet = 0;
        else if (++still >= 5) {
            still = 0;
            n_rescan++;
            /* (a host reset here ended in a controller assert on hardware: only the scan restart) */
            (void)quiet;
            ble_gap_conn_cancel();
            ble_gap_disc_cancel();
            scan_start();
        }
    } else {
        still = 0;
    }
    seen = n_adv;
    while (ev_r != ev_w) printf("%s\n", evlog[ev_r++ % EV_N]);
    {
        static TaskHandle_t host;
        if (!host) host = xTaskGetHandle("nimble_host");
        /* receive pool of the transport (24 blocks): free now and the lowest ever */
        struct os_mempool_info mi;
        int acl_free = -1, acl_min = -1;
        for (struct os_mempool *mp = NULL; (mp = os_mempool_info_get_next(mp, &mi)) != NULL;)
            if (!strcmp(mi.omi_name, "transport_pool_acl")) {
                acl_free = mi.omi_num_free;
                acl_min = mi.omi_min_free;
            }
        printf("pad host events=%u last=%d beat=%u acl_free=%d acl_min=%d rx_acl=%u msys_free=%d stack_free=%u\n",
               (unsigned)n_ev, g_evt, (unsigned)n_beat, acl_free, acl_min, (unsigned)n_rx, os_msys_num_free(),
               host ? (unsigned)uxTaskGetStackHighWaterMark(host) : 0);
    }
    {
        struct os_mempool *mp = acl_pool;
        printf("pad rx alloc=%u fail=%u proc=%u ok=%u again=%u err=%u last_rc=%d len=%u head=%02x%02x%02x%02x%02x%02x%02x%02x "
               "pool_free=%d pool_min=%d pool_n=%d\n",
               (unsigned)n_alloc, (unsigned)n_alloc_fail, (unsigned)n_proc, (unsigned)n_proc_ok, (unsigned)n_proc_again,
               (unsigned)n_proc_err, last_rc, last_len, last_head[0], last_head[1], last_head[2], last_head[3],
               last_head[4], last_head[5], last_head[6], last_head[7], mp ? mp->mp_num_free : -1,
               mp ? mp->mp_min_free : -1, mp ? mp->mp_num_blocks : -1);
    }
    snprintf(text, sizeof text, "%s peer=%s links=%u disc=%d rescans=%u notif=%u len=%u", st, g_peer, (unsigned)n_link,
             g_disc, (unsigned)n_rescan, (unsigned)n_notif, g_len);
    return text;
}
void pad_counts(uint32_t *adv, uint32_t *pads, int *err)
{
    *adv = n_adv;
    *pads = n_pad;
    *err = g_err;
}

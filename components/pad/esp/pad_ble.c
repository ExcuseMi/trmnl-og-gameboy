/* See pad.h. NimBLE central: scan (HID service 0x1812, gamepad appearance or a bonded address), connect, pair
 * (Just Works, bond in NVS), enable notifications of the HID service, parse the reports with xbox_report.c.
 * Log lines over USB: pad scanning / link / connected / report buttons=.. / disconnected. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "pad.h"
#include <stdio.h>
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "xbox_report.h"

#define UUID_HID 0x1812
#define UUID_CCCD 0x2902
#define APPEARANCE_GAMEPAD 0x03c4
#define MAX_CCCD 4
#define SKIP_US 30000000 /* a device that was no usable pad is ignored this long, so it cannot block the scan */

void ble_store_config_init(void);

static uint8_t own_addr_type;
static volatile uint8_t g_buttons;
static volatile bool g_connected;
static uint16_t hid_start, hid_end;
static uint16_t cccd[MAX_CCCD];
static int n_cccd, i_cccd;
static ble_addr_t peer, skip;
static int64_t skip_until;

static int gap_event(struct ble_gap_event *ev, void *arg);

static void scan_start(void)
{
    struct ble_gap_disc_params p = { .itvl = 0x60, .window = 0x30, .passive = 1 };
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY) printf("pad error=scan rc=%d\n", rc);
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
    printf("pad error=%s rc=%d\n", why, rc);
    skip = peer;
    skip_until = esp_timer_get_time() + SKIP_US;
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

static void subscribe_next(uint16_t conn);

static int on_sub(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    if (err->status) printf("pad error=subscribe handle=%u rc=%d\n", cccd[i_cccd], err->status);
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
    printf("pad connected reports=%d\n", n_cccd);
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val_handle,
                  const struct ble_gatt_dsc *dsc, void *arg)
{
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

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (err->status == 0) {
        hid_start = svc->start_handle;
        hid_end = svc->end_handle;
        return 0;
    }
    int rc = err->status;
    if (rc == BLE_HS_EDONE && hid_end) {
        n_cccd = 0;
        rc = ble_gattc_disc_all_dscs(conn, hid_start, hid_end, on_dsc, NULL);
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
    if (f.appearance_is_present && f.appearance == APPEARANCE_GAMEPAD) return true;
    for (int i = 0; i < f.num_uuids16; i++)
        if (ble_uuid_u16(&f.uuids16[i].u) == UUID_HID) return true;
    return false;
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    int rc;
    switch (ev->type) {
    case BLE_GAP_EVENT_DISC:
        if (!wanted(&ev->disc)) return 0;
        peer = ev->disc.addr;
        ble_gap_disc_cancel();
        rc = ble_gap_connect(own_addr_type, &peer, 10000, NULL, gap_event, NULL);
        if (rc) {
            printf("pad error=connect rc=%d\n", rc);
            scan_start();
        }
        return 0;
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status) {
            scan_start();
            return 0;
        }
        printf("pad link %02x:%02x:%02x:%02x:%02x:%02x\n", peer.val[5], peer.val[4], peer.val[3], peer.val[2],
               peer.val[1], peer.val[0]);
        hid_start = hid_end = 0;
        rc = ble_gap_security_initiate(ev->connect.conn_handle);
        if (rc) give_up(ev->connect.conn_handle, "security", rc);
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.status) {
            forget(ev->enc_change.conn_handle); /* e.g. the pad lost its bond: pair again next time */
            printf("pad error=pairing rc=%d\n", ev->enc_change.status);
            ble_gap_terminate(ev->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        rc = ble_gattc_disc_svc_by_uuid(ev->enc_change.conn_handle, BLE_UUID16_DECLARE(UUID_HID), on_svc, NULL);
        if (rc) give_up(ev->enc_change.conn_handle, "discover", rc);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        forget(ev->repeat_pairing.conn_handle);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint8_t d[XBOX_REPORT_LEN + 1];
        uint16_t n = 0;
        xbox_report_t r;
        if (ble_hs_mbuf_to_flat(ev->notify_rx.om, d, sizeof d, &n) || !xbox_parse(d, n, &r)) return 0;
        uint8_t b = xbox_to_gb(&r);
        if (b != g_buttons) {
            g_buttons = b;
            printf("pad report buttons=0x%02x\n", b);
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        g_connected = false;
        g_buttons = 0;
        printf("pad disconnected reason=%d\n", ev->disconnect.reason);
        scan_start();
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        scan_start();
        return 0;
    default:
        return 0;
    }
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc) {
        printf("pad error=address rc=%d\n", rc);
        return;
    }
    printf("pad scanning\n");
    scan_start();
}

static void on_reset(int reason) { printf("pad error=host reset reason=%d\n", reason); }

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
    if (e) {
        printf("pad error=init rc=%d\n", (int)e);
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; /* Just Works */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
}

uint8_t pad_buttons(void) { return g_buttons; }
bool pad_connected(void) { return g_connected; }

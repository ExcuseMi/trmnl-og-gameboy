/* Game Boy on the TRMNL OG e-paper: emulator task paced at 59.7 Hz, panel task pushing changed rows as fast as the
 * panel allows (back to back: the next refresh starts with the first frame the emulator draws after the last one, so
 * a button press waits for the running refresh only). Log line each second: gb fps_emu=.. fps_panel=.. partial_ms=..
 * preset=.. frames=.. hz=.. data_ms=.. refresh_ms=.. pon_ms=.. pof_ms=.. full_every=.. heap_free=.. heap_min=..
 * Without a connected gamepad a hint line stands above the picture. RB / LB on the gamepad step the panel speed
 * preset (gb_speed.h), kept in NVS ("gb" / "speed") and shown for 2 s in the same line.
 * C11. SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "epd.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gb_core.h"
#include "gb_input.h"
#include "gb_render.h"
#include "gb_save.h"
#include "gb_speed.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pad.h"
#include "xbox_report.h"
#include "gb_state.h"
#include "save_flash.h"
#include "state_flash.h"
#include "sdkconfig.h"

#define FRAME_US 16742 /* 1e6 / 59.7275 */
#define IDLE_OFF_US 2000000 /* held panel power goes off after this long without a refresh */
#define SPEED_MSG_US 2000000

#ifdef CONFIG_GB_NO_PANEL
#define NO_PANEL 1
#else
#define NO_PANEL 0
#endif
#ifdef CONFIG_GB_PAD_BLE
#define PAD_BLE 1
#else
#define PAD_BLE 0
#endif

enum { PANEL_BUSY, FRAME_WANTED, FRAME_READY };

static uint8_t fb[GB_FB_SIZE];   /* written by the emulator while FRAME_WANTED, read by the panel task at FRAME_READY */
static uint32_t rows[GB_ROWS];   /* hashes of the rows the panel shows (the controller RAM holds the old plane) */
static volatile int state = PANEL_BUSY;
static volatile uint32_t n_emu, n_panel, partial_ms, data_ms, refresh_ms, pon_ms, pof_ms;
static volatile int speed_want;  /* preset asked for (app_main from NVS, then the emulator task on RB / LB) */
static volatile bool clean_want; /* Y on the gamepad: full refresh now */
static volatile int speed_now;   /* preset the panel task runs */
static int full_every = CONFIG_GB_FULL_EVERY;
static int64_t last_push_us;

static int push(bool full, const tiny_rect_t *r)
{
    if (NO_PANEL) { /* QEMU: no panel, a refresh takes a second */
        vTaskDelay(pdMS_TO_TICKS(1000));
        return 0;
    }
    int64_t t0 = esp_timer_get_time();
    int rc = epd_begin(full ? EPD_FULL : EPD_PARTIAL);
    if (!rc) {
        if (full) {
            tiny_rect_t all = { 0, 0, GB_PANEL_W, GB_PANEL_H };
            rc = epd_write(&all, NULL, fb, GB_STRIDE);
        } else {
            size_t off = (size_t)r->y * GB_STRIDE + r->x / 8;
            rc = epd_write(r, NULL, fb + off, GB_STRIDE);
        }
    }
    int64_t t1 = esp_timer_get_time();
    if (!rc) rc = epd_refresh();
    last_push_us = esp_timer_get_time();
    if (!full) { /* data_ms: registers, LUTs and pixel data over SPI; the others are the BUSY waits (0 = skipped) */
        const epd_stats_t *st = epd_stats();
        partial_ms = (uint32_t)((last_push_us - t0) / 1000);
        data_ms = (uint32_t)((t1 - t0) / 1000);
        refresh_ms = st->refresh_ms;
        pon_ms = st->power_on_ms;
        pof_ms = st->power_off_ms;
    }
    return rc;
}

static void speed_apply(int n)
{
    const gb_speed_t *sp = gb_speed(n);
    epd_tune_t t = { .frames = sp->frames, .hz = sp->hz, .hold_power = sp->hold };
    if (!NO_PANEL) epd_set_tune(&t);
    full_every = sp->full_every ? sp->full_every : CONFIG_GB_FULL_EVERY;
    speed_now = n;
    printf("gb speed=%d frames=%u hz=%u hold=%d full_every=%d\n", n, sp->frames ? sp->frames : GB_SPEED_GX_FRAMES,
           sp->hz ? sp->hz : 50, sp->hold, full_every);
}

static int speed_load(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    nvs_flash_init(); /* pad_init does it again (and erases a damaged partition); without it the preset is 0 */
    if (nvs_open("gb", NVS_READONLY, &h)) return 0;
    if (nvs_get_u8(h, "speed", &v) || v >= GB_SPEED_N) v = 0;
    nvs_close(h);
    return v;
}

static void speed_store(int n)
{
    nvs_handle_t h;
    if (nvs_open("gb", NVS_READWRITE, &h)) return;
    nvs_set_u8(h, "speed", (uint8_t)n);
    nvs_commit(h);
    nvs_close(h);
}

/* Held power (presets 1..): off when nothing was refreshed for a while, so the panel is not left driving. */
static void idle_power(void)
{
    if (!NO_PANEL && epd_powered() && esp_timer_get_time() - last_push_us > IDLE_OFF_US) epd_power_off();
}

static void panel_task(void *arg)
{
    (void)arg;
    epd_pins_t pins = { BOARD_EPD_SCK, BOARD_EPD_MOSI, BOARD_EPD_CS, BOARD_EPD_DC, BOARD_EPD_RST, BOARD_EPD_BUSY,
                        BOARD_EPD_SPI_HZ };
#ifdef CONFIG_GB_EPD_T7
    epd_variant_t variant = EPD_GDEY075T7;
#else
    epd_variant_t variant = EPD_GEN2;
#endif
    /* tiny-paper's settings, tuned by eye on a real TRMNL OG: GxEPD2 partial LUTs, windowed, reps 1, VCOM DC 0x20
     * (solid black, no flash on a partial refresh). */
    epd_cfg_t cfg = { .variant = variant, .part_wave = EPD_PART_GX, .window_refresh = true, .lut_reps = 1,
                      .lut_vcom = 0x20 };
    printf("gb panel variant=%s part=%s\n", epd_variant_name(variant), epd_part_wave_name(cfg.part_wave));
    if (!NO_PANEL && epd_open(&pins, &cfg)) {
        printf("gb error=epd_open\n");
        vTaskDelete(NULL);
    }
    speed_apply(speed_want);
    int partials = 0, line = 0; /* line: 0 none, 1 pairing hint, 2 + n "speed n" */
    bool first = true;
    int64_t msg_until = 0;
    for (;;) {
        int sw = speed_want;
        if (sw != speed_now) { /* between two refreshes */
            speed_apply(sw);
            speed_store(sw);
            msg_until = esp_timer_get_time() + SPEED_MSG_US;
        }
        /* rows above the picture: the emulator never draws there, so this task may */
        int want = esp_timer_get_time() < msg_until ? 2 + speed_now : PAD_BLE && !pad_connected() ? 1 : 0;
        if (want != line) {
            tiny_rect_t lr;
            char msg[] = "speed 0";
            msg[6] = (char)('0' + speed_now);
            line = want;
            gb_fb_line(fb, line >= 2 ? msg : line ? "Hold the pair button on the controller" : NULL, &lr);
            if (!first && partials < full_every && (NO_PANEL || epd_ram_valid())) {
                if (push(false, &lr)) partials = full_every;
                else partials++;
            }
        }
        state = FRAME_WANTED;
        while (state != FRAME_READY) {
            vTaskDelay(1);
            idle_power();
        }
        tiny_rect_t r;
        /* controller RAM lost (a failed power off reset it): full refresh */
        bool full = first || clean_want || partials >= full_every || (!NO_PANEL && !epd_ram_valid());
        clean_want = false;
        if (!full && !gb_fb_diff(rows, fb, &r)) continue;
        state = PANEL_BUSY;
        int rc = push(full, &r);
        if (rc) {
            printf("gb error=push rc=%d\n", rc);
            partials = full_every; /* controller RAM unknown: full refresh next */
        }
        if (full) {
            gb_fb_hash(rows, fb);
            partials = 0;
            first = false;
        } else {
            partials++;
        }
        n_panel++;
    }
}

static void emu_task(void *arg)
{
    (void)arg;
    int64_t next = esp_timer_get_time(), stat = next;
    size_t ram_len;
    uint8_t *ram = gb_core_cart_ram(&ram_len); /* loaded (battery save, then a resumed state) by app_main */
    gb_save_t save;
    if (ram) {
        gb_save_init(&save, ram, ram_len, save_flash_write, next);
    }
    uint32_t seen_writes = gb_core_ram_writes();
    uint32_t last_emu = 0, last_panel = 0;
    const int64_t state_us = (int64_t)CONFIG_GB_STATE_EVERY_S * 1000000;
    int64_t state_next = next + state_us;
    uint16_t raw_prev = 0;
    for (;;) {
#ifdef CONFIG_GB_PAD_BLE
        /* panel speed: RB next, LB previous, on the press only. Not Game Boy input, so not in gb_input */
        uint16_t raw = pad_raw_buttons(), down = raw & (uint16_t)~raw_prev;
        raw_prev = raw;
        if ((down & XBOX_RB) && speed_want < GB_SPEED_N - 1) speed_want = speed_want + 1;
        if ((down & XBOX_LB) && speed_want > 0) speed_want = speed_want - 1;
        if (down & XBOX_Y) clean_want = true; /* full refresh on demand: clears the ghosting */
#else
        (void)raw_prev;
#endif
        bool render = state == FRAME_WANTED;
        gb_core_set_render(render);
        gb_core_frame(gb_input_poll());
        n_emu++;
        if (ram) {
            int64_t t = esp_timer_get_time();
            if (gb_core_ram_writes() != seen_writes) {
                seen_writes = gb_core_ram_writes();
                gb_save_touch(&save, t);
            }
            gb_save_poll(&save, t);
        }
        if (render) state = FRAME_READY;
        if (state_us && esp_timer_get_time() >= state_next) { /* frame boundary: the emulator is not running while it is written */
            state_flash_write();
            state_next = esp_timer_get_time() + state_us;
        }
        int64_t now = esp_timer_get_time();
        next += FRAME_US;
        if (next > now) {
            vTaskDelay(pdMS_TO_TICKS((next - now) / 1000));
        } else if (now - next > 200000) {
            next = now; /* far behind: do not try to catch up */
        }
        now = esp_timer_get_time();
        if (now - stat >= 1000000) {
            const gb_speed_t *sp = gb_speed(speed_now);
            printf("gb fps_emu=%u fps_panel=%u partial_ms=%u preset=%d frames=%u hz=%u data_ms=%u refresh_ms=%u "
                   "pon_ms=%u pof_ms=%u full_every=%d heap_free=%u heap_min=%u\n",
                   (unsigned)(n_emu - last_emu), (unsigned)(n_panel - last_panel), (unsigned)partial_ms, speed_now,
                   sp->frames ? sp->frames : GB_SPEED_GX_FRAMES, sp->hz ? sp->hz : 50, (unsigned)data_ms,
                   (unsigned)refresh_ms, (unsigned)pon_ms, (unsigned)pof_ms, full_every,
                   (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
#ifdef CONFIG_GB_PAD_BLE
            uint32_t adv, pads;
            int perr;
            pad_counts(&adv, &pads, &perr);
            printf("pad state=%s adverts=%u gamepads=%u err=%d buttons=0x%02x\n", pad_state(), (unsigned)adv,
                   (unsigned)pads, perr, pad_buttons());
#endif
            last_emu = n_emu;
            last_panel = n_panel;
            stat += 1000000;
        }
    }
}

void app_main(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "rom");
    const void *rom = NULL;
    esp_partition_mmap_handle_t h;
    if (!p || esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &rom, &h)) {
        printf("gb error=no rom partition\n");
        return;
    }
    gb_fb_clear(fb);
    int rc = gb_core_init(rom, p->size, fb);
    if (rc) {
        for (;;) {
            printf("gb error=no valid ROM in the rom partition (rc=%d): run tools/load_rom.sh\n", rc);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    size_t ram_len;
    uint8_t *ram = gb_core_cart_ram(&ram_len);
    if (ram) save_flash_load(ram, ram_len);
    if (CONFIG_GB_STATE_EVERY_S && state_flash_open(gb_state_rom_id(rom, p->size)) && state_flash_resume() < 0) {
        gb_core_init(rom, p->size, fb); /* a state that did not fit the cart left the core half loaded */
        ram = gb_core_cart_ram(&ram_len);
        if (ram) save_flash_load(ram, ram_len);
    }
    printf("gb rom='%s' shade=%s heap_free=%u\n", gb_core_title(), gb_shade_name(),
           (unsigned)esp_get_free_heap_size());
    gb_input_init();
    speed_want = speed_load();
    xTaskCreate(panel_task, "panel", 6144, NULL, 6, NULL); /* + the NVS write of a preset change */
    xTaskCreate(emu_task, "emu", 8192, NULL, 5, NULL);
}

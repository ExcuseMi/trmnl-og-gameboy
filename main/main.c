/* Game Boy on the TRMNL OG e-paper: emulator task paced at 59.7 Hz, panel task pushing changed rows as fast as the
 * panel allows. Log line each second: gb fps_emu=.. fps_panel=.. partial_ms=.. full_every=.. heap_free=.. heap_min=..
 * Without a connected gamepad a hint line stands above the picture.
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
#include "pad.h"
#include "save_flash.h"
#include "sdkconfig.h"

#define FRAME_US 16742 /* 1e6 / 59.7275 */

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
static volatile uint32_t n_emu, n_panel, partial_ms;

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
    if (!rc) rc = epd_refresh();
    if (!full) partial_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    return rc;
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
    printf("gb panel variant=%s part=%s full_every=%d\n", epd_variant_name(variant), epd_part_wave_name(cfg.part_wave),
           CONFIG_GB_FULL_EVERY);
    if (!NO_PANEL && epd_open(&pins, &cfg)) {
        printf("gb error=epd_open\n");
        vTaskDelete(NULL);
    }
    int partials = 0;
    bool first = true, hint = false;
    for (;;) {
        /* rows above the picture: the emulator never draws there, so this task may */
        bool want = PAD_BLE && !pad_connected();
        if (want != hint) {
            tiny_rect_t lr;
            hint = want;
            gb_fb_line(fb, hint ? "Hold the pair button on the controller" : NULL, &lr);
            if (!first && partials < CONFIG_GB_FULL_EVERY) {
                if (push(false, &lr)) partials = CONFIG_GB_FULL_EVERY;
                else partials++;
            }
        }
        state = FRAME_WANTED;
        while (state != FRAME_READY) vTaskDelay(1);
        tiny_rect_t r;
        bool full = first || partials >= CONFIG_GB_FULL_EVERY;
        if (!full && !gb_fb_diff(rows, fb, &r)) continue;
        state = PANEL_BUSY;
        int rc = push(full, &r);
        if (rc) {
            printf("gb error=push rc=%d\n", rc);
            partials = CONFIG_GB_FULL_EVERY; /* controller RAM unknown: full refresh next */
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
    uint8_t *ram = gb_core_cart_ram(&ram_len);
    gb_save_t save;
    if (ram) {
        save_flash_load(ram, ram_len);
        gb_save_init(&save, ram, ram_len, save_flash_write, next);
    }
    uint32_t seen_writes = gb_core_ram_writes();
    uint32_t last_emu = 0, last_panel = 0;
    for (;;) {
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
        int64_t now = esp_timer_get_time();
        next += FRAME_US;
        if (next > now) {
            vTaskDelay(pdMS_TO_TICKS((next - now) / 1000));
        } else if (now - next > 200000) {
            next = now; /* far behind: do not try to catch up */
        }
        now = esp_timer_get_time();
        if (now - stat >= 1000000) {
            printf("gb fps_emu=%u fps_panel=%u partial_ms=%u full_every=%d heap_free=%u heap_min=%u\n",
                   (unsigned)(n_emu - last_emu), (unsigned)(n_panel - last_panel), (unsigned)partial_ms,
                   CONFIG_GB_FULL_EVERY, (unsigned)esp_get_free_heap_size(),
                   (unsigned)esp_get_minimum_free_heap_size());
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
    printf("gb rom='%s' shade=%s heap_free=%u\n", gb_core_title(), gb_shade_name(),
           (unsigned)esp_get_free_heap_size());
    gb_input_init();
    xTaskCreate(panel_task, "panel", 4096, NULL, 6, NULL);
    xTaskCreate(emu_task, "emu", 8192, NULL, 5, NULL);
}

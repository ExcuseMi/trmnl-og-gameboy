/* Game Boy on the TRMNL OG e-paper: emulator task paced at 59.7 Hz, panel task pushing changed rows as fast as the
 * panel allows. Log line each second: gb fps_emu=.. fps_panel=.. partial_ms=.. full_every=..
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
#include "sdkconfig.h"

#define FRAME_US 16742 /* 1e6 / 59.7275 */

enum { PANEL_BUSY, FRAME_WANTED, FRAME_READY };

static uint8_t fb[GB_FB_SIZE];   /* written by the emulator while FRAME_WANTED, read by the panel task at FRAME_READY */
static uint8_t last[GB_FB_SIZE]; /* what the panel shows */
static volatile int state = PANEL_BUSY;
static volatile uint32_t n_emu, n_panel, partial_ms;

static int push(bool full, const tiny_rect_t *r)
{
    int64_t t0 = esp_timer_get_time();
    int rc = epd_begin(full ? EPD_FULL : EPD_PARTIAL);
    if (!rc) {
        if (full) {
            tiny_rect_t all = { 0, 0, GB_PANEL_W, GB_PANEL_H };
            rc = epd_write(&all, NULL, fb, GB_STRIDE);
        } else {
            size_t off = (size_t)r->y * GB_STRIDE + r->x / 8;
            rc = epd_write(r, last + off, fb + off, GB_STRIDE);
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
#ifdef CONFIG_GB_EPD_GEN2
    epd_variant_t variant = EPD_GEN2;
#else
    epd_variant_t variant = EPD_GDEY075T7;
#endif
    epd_cfg_t cfg = { .variant = variant, .part_wave = EPD_PART_OTP, .window_refresh = true };
    if (epd_open(&pins, &cfg)) {
        printf("gb error=epd_open\n");
        vTaskDelete(NULL);
    }
    int partials = 0;
    bool first = true;
    for (;;) {
        state = FRAME_WANTED;
        while (state != FRAME_READY) vTaskDelay(1);
        tiny_rect_t r;
        bool full = first || partials >= CONFIG_GB_FULL_EVERY;
        if (!full && !gb_fb_diff(last, fb, &r)) continue;
        state = PANEL_BUSY;
        int rc = push(full, &r);
        if (rc) printf("gb error=push rc=%d\n", rc);
        if (full) {
            memcpy(last, fb, sizeof last);
            partials = 0;
            first = false;
        } else {
            for (int y = r.y; y < r.y + r.h; y++)
                memcpy(last + (size_t)y * GB_STRIDE + r.x / 8, fb + (size_t)y * GB_STRIDE + r.x / 8, r.w / 8);
            partials++;
        }
        n_panel++;
    }
}

static void emu_task(void *arg)
{
    (void)arg;
    int64_t next = esp_timer_get_time(), stat = next;
    uint32_t last_emu = 0, last_panel = 0;
    for (;;) {
        bool render = state == FRAME_WANTED;
        gb_core_set_render(render);
        gb_core_frame(gb_input_poll());
        n_emu++;
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
            printf("gb fps_emu=%u fps_panel=%u partial_ms=%u full_every=%d\n", (unsigned)(n_emu - last_emu),
                   (unsigned)(n_panel - last_panel), (unsigned)partial_ms, CONFIG_GB_FULL_EVERY);
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
    memset(last, 0, sizeof last); /* never equal to a frame: the first push is a full refresh anyway */
    int rc = gb_core_init(rom, p->size, fb);
    if (rc) {
        for (;;) {
            printf("gb error=no valid ROM in the rom partition (rc=%d): run tools/load_rom.sh\n", rc);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    printf("gb rom='%s' heap_free=%u\n", gb_core_title(), (unsigned)esp_get_free_heap_size());
    gb_input_init();
    xTaskCreate(panel_task, "panel", 4096, NULL, 6, NULL);
    xTaskCreate(emu_task, "emu", 8192, NULL, 5, NULL);
}

/* Panel speed presets, stepped with RB / LB on the controller while playing. Hardware free (host test:
 * tools/host/test_speed.c). C11. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GB_SPEED_H
#define GB_SPEED_H
#include <stdbool.h>
#include <stdint.h>

#define GB_SPEED_N 6
#define GB_SPEED_GX_FRAMES 70 /* the GxEPD2 partial waveform: 30 + 5 + 30 + 5 */

typedef struct {
    uint8_t frames;      /* drive frames of the one-phase 1-bit LUT, 0 = GxEPD2 waveform (epd_tune_t.frames) */
    uint8_t hz;          /* panel frame rate, 0 = 50 (epd_tune_t.hz) */
    bool hold;           /* charge pumps stay on between partial refreshes (epd_tune_t.hold_power) */
    uint16_t full_every; /* full refresh after this many partials, 0 = CONFIG_GB_FULL_EVERY */
} gb_speed_t;

/* Preset n, clamped to 0 .. GB_SPEED_N - 1. */
const gb_speed_t *gb_speed(int n);
/* Milliseconds the waveform of the preset drives (frames / frame rate), without data transfer and power on/off. */
unsigned gb_speed_wave_ms(const gb_speed_t *s);
#endif

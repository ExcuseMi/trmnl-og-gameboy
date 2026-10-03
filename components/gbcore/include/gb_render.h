/* 160x144 Game Boy frame -> 1-bit 800x480 panel buffer: 3x scale (480x432), centred, Bayer 4x4 dither.
 * Buffer: rows MSB-first, 1 = white (epd.h). C11. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GB_RENDER_H
#define GB_RENDER_H
#include <stdbool.h>
#include <stdint.h>
#include "tiny/types.h"

#define GB_PANEL_W 800
#define GB_PANEL_H 480
#define GB_STRIDE (GB_PANEL_W / 8)
#define GB_FB_SIZE (GB_STRIDE * GB_PANEL_H)
#define GB_SCALE 3
#define GB_X0 160   /* (800 - 480) / 2, a multiple of 8 */
#define GB_Y0 24    /* (480 - 432) / 2 */

/* Fill the whole buffer white. */
void gb_fb_clear(uint8_t *fb);
/* Draw Game Boy line `ly` (160 shades 0 = white .. 3 = black) as three panel rows. */
void gb_render_line(uint8_t *fb, const uint8_t *shades, unsigned ly);
/* Bounding box of the differences between a and b inside the game area, x and w multiples of 8.
 * False if equal. */
bool gb_fb_diff(const uint8_t *a, const uint8_t *b, tiny_rect_t *out);
#endif

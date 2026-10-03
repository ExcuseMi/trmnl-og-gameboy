/* 160x144 Game Boy frame -> 1-bit 800x480 panel buffer: 3x scale (480x432), centred. Shades to 1 bit by
 * GB_SHADE_MODE (build time): 0 = Bayer 4x4 dither (default), 1 = threshold (two dark shades black, two light white).
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
/* "bayer" or "threshold": what this build does. */
const char *gb_shade_name(void);
/* One text line under the game picture (rows GB_LINE_Y .., GB_LINE_H high, as wide as the picture), centred; NULL
 * clears it. Glyphs exist only for the letters of the pairing hint. out (may be NULL): the box to refresh. */
#define GB_LINE_Y 460
#define GB_LINE_H 16
void gb_fb_line(uint8_t *fb, const char *text, tiny_rect_t *out);
#define GB_ROWS (144 * GB_SCALE)
/* Row hashes of the game area (GB_ROWS entries) stand in for a copy of the frame the panel shows. */
void gb_fb_hash(uint32_t *hash, const uint8_t *fb);
/* Rows of the game area whose hash differs from `hash` (updated) as one full-width box. False if none. */
bool gb_fb_diff(uint32_t *hash, const uint8_t *fb, tiny_rect_t *out);
#endif

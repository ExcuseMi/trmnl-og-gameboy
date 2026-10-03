/* See gb_render.h. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_render.h"
#include <string.h>

static const uint8_t bayer4[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
/* White fraction per shade (x/16): white, light, dark, black. A pixel is white when level > threshold. */
static const uint8_t level[4] = { 16, 10, 5, 0 };

void gb_fb_clear(uint8_t *fb) { memset(fb, 0xff, GB_FB_SIZE); }

void gb_render_line(uint8_t *fb, const uint8_t *shades, unsigned ly)
{
    for (unsigned k = 0; k < GB_SCALE; k++) {
        unsigned y = GB_Y0 + ly * GB_SCALE + k;
        const uint8_t *th = bayer4[y & 3];
        uint8_t *row = fb + y * GB_STRIDE + GB_X0 / 8;
        unsigned x = 0, gx = 0;
        for (unsigned b = 0; b < 160 * GB_SCALE / 8; b++) {
            uint8_t v = 0;
            for (unsigned i = 0; i < 8; i++, x++) {
                gx = x / GB_SCALE;
                v = (uint8_t)(v << 1 | (level[shades[gx] & 3] > th[x & 3]));
            }
            row[b] = v;
        }
    }
}

static uint32_t row_hash(const uint8_t *p)
{
    uint32_t h = 2166136261u; /* FNV-1a over the game area of one row */
    for (unsigned i = GB_X0 / 8; i < (GB_X0 + 160 * GB_SCALE) / 8; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

void gb_fb_hash(uint32_t *hash, const uint8_t *fb)
{
    for (unsigned i = 0; i < GB_ROWS; i++) hash[i] = row_hash(fb + (GB_Y0 + i) * GB_STRIDE);
}

bool gb_fb_diff(uint32_t *hash, const uint8_t *fb, tiny_rect_t *out)
{
    int y0 = -1, y1 = 0;
    for (unsigned i = 0; i < GB_ROWS; i++) {
        uint32_t h = row_hash(fb + (GB_Y0 + i) * GB_STRIDE);
        if (h == hash[i]) continue;
        hash[i] = h;
        if (y0 < 0) y0 = (int)i;
        y1 = (int)i;
    }
    if (y0 < 0) return false;
    out->x = GB_X0;
    out->y = (int16_t)(GB_Y0 + y0);
    out->w = 160 * GB_SCALE;
    out->h = (uint16_t)(y1 - y0 + 1);
    return true;
}

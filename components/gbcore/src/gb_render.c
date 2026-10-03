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

bool gb_fb_diff(const uint8_t *a, const uint8_t *b, tiny_rect_t *out)
{
    int y0 = -1, y1 = 0, x0 = GB_STRIDE, x1 = -1;
    for (unsigned y = GB_Y0; y < GB_Y0 + 144 * GB_SCALE; y++) {
        const uint8_t *pa = a + y * GB_STRIDE, *pb = b + y * GB_STRIDE;
        int f = -1, l = -1;
        for (unsigned i = GB_X0 / 8; i < (GB_X0 + 160 * GB_SCALE) / 8; i++)
            if (pa[i] != pb[i]) {
                if (f < 0) f = (int)i;
                l = (int)i;
            }
        if (f < 0) continue;
        if (y0 < 0) y0 = (int)y;
        y1 = (int)y;
        if (f < x0) x0 = f;
        if (l > x1) x1 = l;
    }
    if (y0 < 0) return false;
    out->x = (int16_t)(x0 * 8);
    out->y = (int16_t)y0;
    out->w = (uint16_t)((x1 - x0 + 1) * 8);
    out->h = (uint16_t)(y1 - y0 + 1);
    return true;
}

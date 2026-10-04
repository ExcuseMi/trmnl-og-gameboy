/* See gb_render.h. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_render.h"
#include <string.h>

static const uint8_t bayer4[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
/* White fraction per shade (x/16): white, light, dark, black. A pixel is white when level > threshold. */
#ifndef GB_SHADE_MODE
#define GB_SHADE_MODE 0
#endif
#if GB_SHADE_MODE == 1
static const uint8_t level[4] = { 16, 16, 0, 0 };
const char *gb_shade_name(void) { return "threshold"; }
#else
static const uint8_t level[4] = { 16, 10, 5, 0 };
const char *gb_shade_name(void) { return "bayer"; }
#endif

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

/* 5x7 glyphs, one byte per row, bit 4 = left column. Drawn at 2x. */
static const struct { char c; uint8_t rows[7]; } glyphs[] = {
    { 'H', { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } }, { 'a', { 0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f } },
    { 'b', { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1e } }, { 'c', { 0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e } },
    { 'd', { 0x01, 0x01, 0x0d, 0x13, 0x11, 0x11, 0x0f } }, { 'e', { 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e } },
    { 'h', { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11 } }, { 'i', { 0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e } },
    { 'l', { 0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e } }, { 'n', { 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11 } },
    { 'o', { 0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e } }, { 'p', { 0x00, 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10 } },
    { 'r', { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10 } }, { 't', { 0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06 } },
    { 'u', { 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d } }, { 's', { 0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e } },
    { '0', { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e } }, { '1', { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e } },
    { '2', { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f } }, { '3', { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e } },
    { '4', { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 } }, { '5', { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e } },
};

void gb_fb_line(uint8_t *fb, const char *text, tiny_rect_t *out)
{
    const unsigned w = 160 * GB_SCALE;
    for (unsigned y = GB_LINE_Y; y < GB_LINE_Y + GB_LINE_H; y++) memset(fb + y * GB_STRIDE + GB_X0 / 8, 0xff, w / 8);
    if (out) *out = (tiny_rect_t){ GB_X0, GB_LINE_Y, (uint16_t)w, GB_LINE_H };
    if (!text) return;
    size_t len = strlen(text);
    if (len * 12 > w) len = w / 12;
    unsigned x0 = GB_X0 + (w - (unsigned)len * 12) / 2;
    for (size_t n = 0; n < len; n++, x0 += 12) {
        for (unsigned g = 0; g < sizeof glyphs / sizeof glyphs[0]; g++) {
            if (glyphs[g].c != text[n]) continue;
            for (unsigned py = 0; py < 14; py++)
                for (unsigned px = 0; px < 10; px++)
                    if (glyphs[g].rows[py / 2] >> (4 - px / 2) & 1) {
                        unsigned x = x0 + px;
                        fb[(GB_LINE_Y + 1 + py) * GB_STRIDE + x / 8] &= (uint8_t)~(0x80 >> (x & 7));
                    }
            break;
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

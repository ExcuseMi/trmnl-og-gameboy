/* Unit test of the fast partial LUT, the PLL table and the speed presets. make -C tools/host test. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include "epd_lut.h"
#include "gb_speed.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    static uint8_t l[EPD_LUT_N][EPD_LUT_LEN];
    static const unsigned counts[] = { 1, 4, 6, 10, 20, 255 };
    for (unsigned c = 0; c < sizeof counts / sizeof counts[0]; c++) {
        unsigned n = counts[c];
        CHECK(epd_lut_build_fast(n, l) == n);
        for (int i = 0; i < EPD_LUT_N; i++) {
            CHECK(epd_lut_frames(l[i]) == n); /* all six run equally long */
            for (int b = 6; b < EPD_LUT_LEN; b++) CHECK(l[i][b] == 0); /* one group only */
        }
        /* unchanged pixels, border and VCOM are not driven */
        CHECK(epd_lut_drive_frames(l[0]) == 0);
        CHECK(epd_lut_drive_frames(l[1]) == 0);
        CHECK(epd_lut_drive_frames(l[4]) == 0);
        CHECK(epd_lut_drive_frames(l[5]) == 0);
        /* black to white: VDL, white to black: VDH, the whole time (signs as in the bb_epaper and GxEPD2 tables) */
        CHECK(epd_lut_drive_frames(l[2]) == n && epd_lut_dc(l[2]) == -(int)n);
        CHECK(epd_lut_drive_frames(l[3]) == n && epd_lut_dc(l[3]) == (int)n);
    }
    CHECK(epd_lut_build_fast(0, l) == 1);
    CHECK(epd_lut_build_fast(1000, l) == 255 && l[3][1] == 255);
    /* the GxEPD2 table ends its transitions on the same levels */
    epd_lut_build(EPD_LUTSET_GX, 1, l);
    CHECK(epd_lut_frames(l[3]) == GB_SPEED_GX_FRAMES);
    CHECK((l[2][0] & 0x0c) == 0x08 && (l[3][0] & 0x0c) == 0x04);
    CHECK(epd_lut_drive_frames(l[1]) == 0 && epd_lut_drive_frames(l[4]) == 0);

    CHECK(epd_pll_reg(0) == EPD_PLL_50HZ && epd_pll_reg(50) == 0x06 && epd_pll_hz(0x06) == 50);
    CHECK(epd_pll_reg(100) == 0x0b && epd_pll_reg(200) == 0x0f && epd_pll_reg(255) == 0x0f);
    CHECK(epd_pll_reg(120) == 0x0c && epd_pll_reg(1) == 0x00 && epd_pll_hz(0x0f) == 200 && epd_pll_hz(0xff) == 200);

    /* preset 0 changes nothing; every next one is faster and cleans up at least as often */
    const gb_speed_t *p0 = gb_speed(0);
    CHECK(p0->frames == 0 && p0->hz == 0 && !p0->hold && p0->full_every == 0);
    CHECK(gb_speed(-1) == p0 && gb_speed(99) == gb_speed(GB_SPEED_N - 1));
    for (int i = 1; i < GB_SPEED_N; i++) {
        const gb_speed_t *a = gb_speed(i - 1), *b = gb_speed(i);
        CHECK(b->hold);
        CHECK(gb_speed_wave_ms(b) <= gb_speed_wave_ms(a));
        CHECK(b->full_every > 0 && (a->full_every == 0 || b->full_every < a->full_every));
        CHECK(epd_pll_hz(epd_pll_reg(b->hz)) == (b->hz ? b->hz : 50u)); /* an exact PLL step */
    }
    CHECK(gb_speed_wave_ms(p0) == 1400 && gb_speed_wave_ms(gb_speed(GB_SPEED_N - 1)) == 20);
    printf(fails ? "test_speed: %d FAILED\n" : "test_speed: ok\n", fails);
    return fails != 0;
}

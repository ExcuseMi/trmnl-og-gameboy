/* Unit test of the Xbox BLE report parser. make -C tools/host test. C11. SPDX-License-Identifier: GPL-3.0-or-later
 * Sample reports: no capture of a real controller is published, so they are built by hand byte for byte from the
 * layout of the HID report descriptors in https://github.com/DJm00n/ControllersInfo (xboxone/, models 1914 and
 * 1708 firmware 5.17, identical): 4 x u16 sticks, 2 x 10 bit triggers in u16, 4 bit hat (1..8, 0 = none), 15 buttons
 * in 16 bits, 1 bit Share in a last byte. Replace them with recorded ones when a capture is at hand. */
#include <stdio.h>
#include <string.h>
#include "xbox_report.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static uint8_t gb(const uint8_t *d, size_t n)
{
    xbox_report_t r;
    return xbox_parse(d, n, &r) ? xbox_to_gb(&r) : 0xff;
}

int main(void)
{
    /* idle: sticks centred (0x8000), triggers 0, hat 0, no buttons */
    const uint8_t idle[16] = { 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0, 0, 0, 0, 0, 0, 0, 0 };
    /* A + Menu held, LT half (0x1ff), RT full (0x3ff), hat north */
    const uint8_t am[16] = { 0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0xff, 0x01, 0xff, 0x03, 0x01, 0x01, 0x08, 0 };
    /* hat south-east, B + View + Share */
    const uint8_t se[16] = { 0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0, 0, 0, 0, 0x04, 0x02, 0x04, 0x01 };
    /* left stick hard right and up (LX 0xffff, LY 0), hat none */
    const uint8_t st[16] = { 0xff, 0xff, 0x00, 0x00, 0, 0x80, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0 };
    /* left stick slightly right (inside the dead zone) */
    const uint8_t sl[16] = { 0x00, 0x90, 0, 0x80, 0, 0x80, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0 };
    xbox_report_t r;

    CHECK(xbox_parse(idle, 16, &r) && r.lx == 0x8000 && r.hat == 0 && r.buttons == 0 && !r.share);
    CHECK(gb(idle, 16) == 0);
    CHECK(xbox_parse(am, 16, &r) && r.lt == 0x1ff && r.rt == 0x3ff && r.hat == 1 && r.buttons == (XBOX_A | XBOX_MENU));
    CHECK(gb(am, 16) == (0x01 | 0x08 | 0x40));
    CHECK(xbox_parse(se, 16, &r) && r.share && r.buttons == (XBOX_B | XBOX_VIEW));
    CHECK(gb(se, 16) == (0x02 | 0x04 | 0x10 | 0x80));
    CHECK(gb(st, 16) == (0x10 | 0x40));
    CHECK(gb(sl, 16) == 0);
    /* with the report ID in front */
    uint8_t withid[17] = { 1 };
    memcpy(withid + 1, am, 16);
    CHECK(gb(withid, 17) == (0x01 | 0x08 | 0x40));
    CHECK(xbox_parse(am, 15, &r) && !r.share && xbox_parse(am, 18, &r) && !xbox_parse(am, 14, &r));
    /* all eight hat directions */
    static const uint8_t want[9] = { 0, 0x40, 0x50, 0x10, 0x90, 0x80, 0xa0, 0x20, 0x60 };
    for (int h = 0; h <= 8; h++) {
        uint8_t d[16];
        memcpy(d, idle, 16);
        d[12] = (uint8_t)h;
        CHECK(gb(d, 16) == want[h]);
    }
    printf(fails ? "xbox test FAILED\n" : "xbox test ok\n");
    return fails != 0;
}

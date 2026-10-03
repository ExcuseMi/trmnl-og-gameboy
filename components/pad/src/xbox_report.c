/* See xbox_report.h. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#include "xbox_report.h"

#define STICK_DEAD 14000 /* distance from the centre 32768 that counts as a direction (about 43 %) */

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool xbox_parse(const uint8_t *d, size_t n, xbox_report_t *r)
{
    if (n == XBOX_REPORT_LEN + 1 && d[0] == 1) {
        d++;
        n--;
    }
    if (n != XBOX_REPORT_LEN) return false;
    r->lx = u16(d);
    r->ly = u16(d + 2);
    r->rx = u16(d + 4);
    r->ry = u16(d + 6);
    r->lt = u16(d + 8) & 0x3ff;
    r->rt = u16(d + 10) & 0x3ff;
    r->hat = d[12] & 0x0f;
    r->buttons = u16(d + 13) & 0x7fff;
    r->share = d[15] & 1;
    return true;
}

uint8_t xbox_to_gb(const xbox_report_t *r)
{
    uint8_t o = 0;
    if (r->buttons & XBOX_A) o |= 0x01;
    if (r->buttons & XBOX_B) o |= 0x02;
    if (r->buttons & XBOX_VIEW) o |= 0x04;
    if (r->buttons & XBOX_MENU) o |= 0x08;
    /* hat 1..8 clockwise from north: N NE E SE S SW W NW */
    if (r->hat == 1 || r->hat == 2 || r->hat == 8) o |= 0x40;
    if (r->hat >= 2 && r->hat <= 4) o |= 0x10;
    if (r->hat >= 4 && r->hat <= 6) o |= 0x80;
    if (r->hat >= 6 && r->hat <= 8) o |= 0x20;
    if (r->lx > 32768 + STICK_DEAD) o |= 0x10;
    if (r->lx < 32768 - STICK_DEAD) o |= 0x20;
    if (r->ly < 32768 - STICK_DEAD) o |= 0x40;
    if (r->ly > 32768 + STICK_DEAD) o |= 0x80;
    return o;
}

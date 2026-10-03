/* Xbox Wireless Controller BLE input report (HID report ID 1), models 1708 (firmware 5.x) and 1914. C11.
 * Layout (payload without the report ID, 16 bytes, little endian), from the HID report descriptor published at
 * https://github.com/DJm00n/ControllersInfo/tree/master/xboxone (xboxone_model_1914_bluetoothle_..., 1708 firmware 5.x;
 * both descriptors are identical):
 *   0..7  LX LY RX RY  uint16 each, 0..65535, centre about 32768, Y grows downwards
 *   8..9  LT uint16 (10 bit, 0..1023)    10..11 RT uint16 (10 bit)
 *   12    hat switch, low 4 bits: 0 = none, 1 = N, 2 = NE, 3 = E, 4 = SE, 5 = S, 6 = SW, 7 = W, 8 = NW
 *   13..14 buttons, 15 bit field (usage 1..15), 15 Share/Record in bit 0 (model 1914 only)
 * Button bits: A 0, B 1, X 3, Y 4, LB 6, RB 7, View 10, Menu 11, Xbox 12, LS 13, RS 14 (Linux xpadneo / Bluepad32 map).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef XBOX_REPORT_H
#define XBOX_REPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XBOX_REPORT_LEN 16

enum {
    XBOX_A = 1 << 0, XBOX_B = 1 << 1, XBOX_X = 1 << 3, XBOX_Y = 1 << 4, XBOX_LB = 1 << 6, XBOX_RB = 1 << 7,
    XBOX_VIEW = 1 << 10, XBOX_MENU = 1 << 11, XBOX_XBOX = 1 << 12, XBOX_LS = 1 << 13, XBOX_RS = 1 << 14,
};

typedef struct {
    uint16_t lx, ly, rx, ry, lt, rt;
    uint8_t hat;
    uint16_t buttons;
    bool share;
} xbox_report_t;

/* d: 16 bytes, or 17 with the report ID 1 in front. False on any other length. */
bool xbox_parse(const uint8_t *d, size_t n, xbox_report_t *r);
/* Game Boy buttons (gb_input.h bits): d-pad and left stick = d-pad, A = A, B = B, Menu = Start, View = Select. */
uint8_t xbox_to_gb(const xbox_report_t *r);
#endif

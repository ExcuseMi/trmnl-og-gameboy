/* BLE gamepad (Xbox Wireless Controller) as a second input source next to the OG's button. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PAD_H
#define PAD_H
#include <stdbool.h>
#include <stdint.h>

/* Starts NimBLE (central): scan for HID devices, connect, bond, subscribe. Reconnects on its own. */
void pad_init(void);
/* Pressed buttons now (gb_input.h bits), 0 without a controller. */
uint8_t pad_buttons(void);
/* True while a controller sends reports. */
bool pad_connected(void);
#endif

/* Input source for the emulator. A button, later a BLE gamepad, implements this. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GB_INPUT_H
#define GB_INPUT_H
#include <stdint.h>

/* Bits as Peanut-GB JOYPAD_*: A 0x01, B 0x02, SELECT 0x04, START 0x08, RIGHT 0x10, LEFT 0x20, UP 0x40, DOWN 0x80. */
void gb_input_init(void);
/* Pressed buttons now (1 = pressed). Called once per emulated frame. */
uint8_t gb_input_poll(void);
#endif

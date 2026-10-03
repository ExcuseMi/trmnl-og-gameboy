/* Peanut-GB wrapped as a singleton that renders into a 1-bit panel buffer. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GB_CORE_H
#define GB_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* rom: memory-mapped or in RAM, must outlive the core. 0 = ok. Cart RAM is allocated (size from the header). */
int gb_core_init(const uint8_t *rom, size_t len, uint8_t *fb);
/* Render the lines of the next frames into fb (else they are skipped, emulation only). */
void gb_core_set_render(bool on);
/* Run one frame (70224 cycles) with `pressed` buttons (gb_input.h bits). */
void gb_core_frame(uint8_t pressed);
const char *gb_core_title(void);
/* Battery RAM (NULL, 0 if the cart has none), to load before the first frame and to save. */
uint8_t *gb_core_cart_ram(size_t *len);
/* Counts the cart's writes to its RAM. */
uint32_t gb_core_ram_writes(void);
/* Save state: the payload is the emulator struct followed by the cart RAM, read and written in pieces so no second
 * copy is needed. Function pointers are zeroed in what is read and restored after a load, never taken from flash.
 * Call between frames. Size is fixed after gb_core_init. */
size_t gb_core_state_size(void);
void gb_core_state_read(size_t off, uint8_t *buf, size_t n);
/* load: begin, write the pieces in order, end. False if the loaded state does not fit the cart (call gb_core_init). */
void gb_core_state_begin(void);
void gb_core_state_write(size_t off, const uint8_t *buf, size_t n);
bool gb_core_state_end(void);
#endif

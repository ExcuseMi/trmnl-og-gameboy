/* Peanut-GB wrapped as a singleton that renders into a 1-bit panel buffer. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GB_CORE_H
#define GB_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* rom: memory-mapped or in RAM, must outlive the core. 0 = ok. Cart RAM is allocated, not saved. */
int gb_core_init(const uint8_t *rom, size_t len, uint8_t *fb);
/* Render the lines of the next frames into fb (else they are skipped, emulation only). */
void gb_core_set_render(bool on);
/* Run one frame (70224 cycles) with `pressed` buttons (gb_input.h bits). */
void gb_core_frame(uint8_t pressed);
const char *gb_core_title(void);
#endif

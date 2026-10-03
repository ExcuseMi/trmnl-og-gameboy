/* Minimal subset of tiny-paper firmware/include/tiny/types.h (9fcff4a7). C11. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TINY_TYPES_H
#define TINY_TYPES_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    int16_t x, y;
    uint16_t w, h;
} tiny_rect_t;

#endif

/* Save partition access. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SAVE_FLASH_H
#define SAVE_FLASH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Fill ram from the 'save' partition. False (ram zeroed) if empty or invalid. */
bool save_flash_load(uint8_t *ram, size_t size);
int save_flash_write(const uint8_t *ram, size_t size);
#endif

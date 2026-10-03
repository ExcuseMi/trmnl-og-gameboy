/* Battery save policy: write cart RAM back when it changed (CRC) and has been quiet for GB_SAVE_QUIET_US, never more
 * often than GB_SAVE_MIN_GAP_US. Hardware free (flash access is the `write` callback). C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GB_SAVE_H
#define GB_SAVE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GB_SAVE_QUIET_US 2000000
#define GB_SAVE_MIN_GAP_US 10000000
#define GB_SAVE_HDR_SIZE 16 /* magic, size, crc32 of the data, reserved; stored in front of the data, written last */

typedef struct {
    uint8_t *ram;
    size_t size;
    uint32_t crc;           /* of what is stored */
    int64_t last_change_us, last_write_us;
    bool dirty;
    int (*write)(const uint8_t *ram, size_t size); /* 0 = ok */
} gb_save_t;

uint32_t gb_crc32(const uint8_t *p, size_t n);
/* Stored image = header + data. Fills hdr for data. */
void gb_save_hdr_make(uint8_t hdr[GB_SAVE_HDR_SIZE], const uint8_t *data, size_t size);
/* True if hdr belongs to `size` bytes of data and the data matches its CRC. */
bool gb_save_hdr_ok(const uint8_t hdr[GB_SAVE_HDR_SIZE], const uint8_t *data, size_t size);
/* ram holds the loaded (or empty) cart RAM. */
void gb_save_init(gb_save_t *s, uint8_t *ram, size_t size, int (*write)(const uint8_t *, size_t), int64_t now_us);
/* The cart wrote to its RAM. */
void gb_save_touch(gb_save_t *s, int64_t now_us);
/* Call regularly. True if it wrote. A failed write is retried after the minimum gap. */
bool gb_save_poll(gb_save_t *s, int64_t now_us);
#endif

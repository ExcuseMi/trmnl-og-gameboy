/* See gb_save.h. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_save.h"
#include <string.h>

#define MAGIC 0x56534247u /* "GBSV" little endian */

uint32_t gb_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = ~0u;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

void gb_save_hdr_make(uint8_t hdr[GB_SAVE_HDR_SIZE], const uint8_t *data, size_t size)
{
    memset(hdr, 0, GB_SAVE_HDR_SIZE);
    put32(hdr, MAGIC);
    put32(hdr + 4, (uint32_t)size);
    put32(hdr + 8, gb_crc32(data, size));
}

bool gb_save_hdr_ok(const uint8_t hdr[GB_SAVE_HDR_SIZE], const uint8_t *data, size_t size)
{
    return get32(hdr) == MAGIC && get32(hdr + 4) == size && get32(hdr + 8) == gb_crc32(data, size);
}

size_t gb_save_sectors(size_t size) { return (GB_SAVE_HDR_SIZE + size + GB_SAVE_SECTOR - 1) / GB_SAVE_SECTOR; }

bool gb_save_image_eq(const uint8_t hdr[GB_SAVE_HDR_SIZE], const uint8_t *data, size_t size, size_t off,
                      const uint8_t *flash, size_t n)
{
    for (size_t i = 0; i < n; i++, off++) {
        uint8_t b = off < GB_SAVE_HDR_SIZE ? hdr[off] : off - GB_SAVE_HDR_SIZE < size ? data[off - GB_SAVE_HDR_SIZE] : 0xff;
        if (flash[i] != b) return false;
    }
    return true;
}

void gb_save_init(gb_save_t *s, uint8_t *ram, size_t size, int (*write)(const uint8_t *, size_t), int64_t now_us)
{
    memset(s, 0, sizeof *s);
    s->ram = ram;
    s->size = size;
    s->write = write;
    s->crc = gb_crc32(ram, size);
    s->last_write_us = now_us - GB_SAVE_MIN_GAP_US; /* the first save may go out at once */
}

void gb_save_touch(gb_save_t *s, int64_t now_us)
{
    s->dirty = true;
    s->last_change_us = now_us;
}

bool gb_save_poll(gb_save_t *s, int64_t now_us)
{
    if (!s->dirty || now_us - s->last_change_us < GB_SAVE_QUIET_US || now_us - s->last_write_us < GB_SAVE_MIN_GAP_US)
        return false;
    uint32_t crc = gb_crc32(s->ram, s->size);
    if (crc == s->crc) { /* rewritten with the same values */
        s->dirty = false;
        return false;
    }
    s->last_write_us = now_us;
    if (s->write(s->ram, s->size)) return false; /* stays dirty: retried after the gap */
    s->crc = crc;
    s->dirty = false;
    return true;
}

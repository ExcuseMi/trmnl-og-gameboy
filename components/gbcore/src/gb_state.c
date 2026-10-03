/* See gb_state.h. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_state.h"
#include <string.h>

#define MAGIC 0x54534247u /* "GBST" little endian */

uint32_t gb_crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    uint32_t c = ~crc;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

uint32_t gb_state_rom_id(const uint8_t *rom, size_t len)
{
    return len < 0x150 ? 0 : gb_crc32_update(0, rom + 0x100, 0x50);
}

size_t gb_state_slot_size(size_t payload)
{
    return (GB_STATE_HDR_SIZE + payload + GB_STATE_SECTOR - 1) / GB_STATE_SECTOR * GB_STATE_SECTOR;
}

void gb_state_hdr_make(uint8_t hdr[GB_STATE_HDR_SIZE], uint32_t seq, size_t size, uint32_t rom_id, uint32_t payload_crc)
{
    memset(hdr, 0, GB_STATE_HDR_SIZE);
    put32(hdr, MAGIC);
    put32(hdr + 4, GB_STATE_VERSION);
    put32(hdr + 8, seq);
    put32(hdr + 12, (uint32_t)size);
    put32(hdr + 16, rom_id);
    put32(hdr + 20, payload_crc);
    put32(hdr + 28, gb_crc32_update(0, hdr, 28));
}

int gb_state_hdr_check(const uint8_t hdr[GB_STATE_HDR_SIZE], uint32_t rom_id, size_t size, uint32_t *seq, uint32_t *crc)
{
    if (get32(hdr) != MAGIC) return GB_STATE_EMPTY;
    if (get32(hdr + 28) != gb_crc32_update(0, hdr, 28)) return GB_STATE_BAD_HDR;
    if (get32(hdr + 4) != GB_STATE_VERSION) return GB_STATE_BAD_VERSION;
    if (get32(hdr + 12) != size) return GB_STATE_BAD_SIZE;
    if (get32(hdr + 16) != rom_id) return GB_STATE_BAD_ROM;
    if (seq) *seq = get32(hdr + 8);
    if (crc) *crc = get32(hdr + 20);
    return GB_STATE_OK;
}

const char *gb_state_why(int why)
{
    switch (why) {
    case GB_STATE_OK: return "ok";
    case GB_STATE_EMPTY: return "empty";
    case GB_STATE_BAD_HDR: return "header CRC";
    case GB_STATE_BAD_VERSION: return "format version";
    case GB_STATE_BAD_SIZE: return "size";
    case GB_STATE_BAD_ROM: return "other ROM";
    default: return "?";
    }
}

bool gb_state_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

void gb_state_pick(const uint8_t hdr0[GB_STATE_HDR_SIZE], const uint8_t hdr1[GB_STATE_HDR_SIZE], uint32_t rom_id,
                   size_t size, gb_state_pick_t *p)
{
    const uint8_t *h[2] = { hdr0, hdr1 };
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 2; i++) {
        p->why[i] = gb_state_hdr_check(h[i], rom_id, size, &p->seq[i], &p->crc[i]);
        if (p->why[i] == GB_STATE_OK) p->order[p->n++] = i;
    }
    if (p->n == 2 && gb_state_newer(p->seq[1], p->seq[0])) {
        p->order[0] = 1;
        p->order[1] = 0;
    }
}

void gb_state_next(const uint8_t hdr0[GB_STATE_HDR_SIZE], const uint8_t hdr1[GB_STATE_HDR_SIZE], int *slot,
                   uint32_t *seq)
{
    const uint8_t *h[2] = { hdr0, hdr1 };
    bool ok[2];
    uint32_t s[2] = { 0, 0 };
    for (int i = 0; i < 2; i++) {
        /* intact for any ROM and size: those are matched with the header's own values */
        ok[i] = get32(h[i]) == MAGIC && get32(h[i] + 28) == gb_crc32_update(0, h[i], 28) &&
                get32(h[i] + 4) == GB_STATE_VERSION;
        if (ok[i]) s[i] = get32(h[i] + 8);
    }
    int newest = -1;
    if (ok[0] && ok[1]) newest = gb_state_newer(s[1], s[0]) ? 1 : 0;
    else if (ok[0]) newest = 0;
    else if (ok[1]) newest = 1;
    *slot = newest < 0 ? 0 : 1 - newest;
    *seq = newest < 0 ? 1 : s[newest] + 1;
}

/* Unit test of the save state slots (gb_state.c) and the core snapshot round trip. make -C tools/host test. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb_core.h"
#include "gb_render.h"
#include "gb_state.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void mk(uint8_t h[GB_STATE_HDR_SIZE], uint32_t seq, size_t size, uint32_t rom) { gb_state_hdr_make(h, seq, size, rom, 0xabcd); }

static void headers(void)
{
    uint8_t a[GB_STATE_HDR_SIZE], b[GB_STATE_HDR_SIZE], none[GB_STATE_HDR_SIZE];
    gb_state_pick_t p;
    uint32_t seq, crc;
    int slot;
    memset(none, 0xff, sizeof none);
    CHECK(gb_state_slot_size(1) == 4096 && gb_state_slot_size(4096 - 32) == 4096 && gb_state_slot_size(4096 - 31) == 8192);
    CHECK(gb_crc32_update(gb_crc32_update(0, (const uint8_t *)"1234", 4), (const uint8_t *)"56789", 5) == 0xcbf43926u);

    mk(a, 7, 1000, 0x1234);
    CHECK(gb_state_hdr_check(a, 0x1234, 1000, &seq, &crc) == GB_STATE_OK && seq == 7 && crc == 0xabcd); /* round trip */
    CHECK(gb_state_hdr_check(a, 0x1235, 1000, NULL, NULL) == GB_STATE_BAD_ROM);                         /* wrong ROM */
    CHECK(gb_state_hdr_check(a, 0x1234, 1001, NULL, NULL) == GB_STATE_BAD_SIZE);
    CHECK(gb_state_hdr_check(none, 0x1234, 1000, NULL, NULL) == GB_STATE_EMPTY);
    memcpy(b, a, sizeof b);
    b[9] ^= 1; /* corrupted sequence: header CRC */
    CHECK(gb_state_hdr_check(b, 0x1234, 1000, NULL, NULL) == GB_STATE_BAD_HDR);
    memcpy(b, a, sizeof b);
    b[4] = 2; /* other format version (header CRC fixed up by hand is not possible: refused either way) */
    CHECK(gb_state_hdr_check(b, 0x1234, 1000, NULL, NULL) != GB_STATE_OK);

    /* older vs newer */
    mk(a, 5, 1000, 1);
    mk(b, 6, 1000, 1);
    gb_state_pick(a, b, 1, 1000, &p);
    CHECK(p.n == 2 && p.order[0] == 1 && p.order[1] == 0);
    gb_state_pick(b, a, 1, 1000, &p);
    CHECK(p.n == 2 && p.order[0] == 0);
    gb_state_next(a, b, &slot, &seq);
    CHECK(slot == 0 && seq == 7);
    gb_state_next(b, a, &slot, &seq);
    CHECK(slot == 1 && seq == 7);
    /* one slot only, or none, or one of another ROM */
    gb_state_pick(a, none, 1, 1000, &p);
    CHECK(p.n == 1 && p.order[0] == 0 && p.why[1] == GB_STATE_EMPTY);
    gb_state_next(none, a, &slot, &seq);
    CHECK(slot == 0 && seq == 6);
    gb_state_next(none, none, &slot, &seq);
    CHECK(slot == 0 && seq == 1);
    gb_state_pick(a, b, 2, 1000, &p);
    CHECK(p.n == 0 && p.why[0] == GB_STATE_BAD_ROM);
    /* sequence wrap: 0 is newer than 0xffffffff */
    mk(a, 0xffffffffu, 1000, 1);
    mk(b, 0, 1000, 1);
    gb_state_pick(a, b, 1, 1000, &p);
    CHECK(p.order[0] == 1);
    gb_state_next(a, b, &slot, &seq);
    CHECK(slot == 0 && seq == 1);
    gb_state_next(b, a, &slot, &seq);
    CHECK(slot == 1 && seq == 1);
    CHECK(gb_state_newer(1, 0xfffffff0u) && !gb_state_newer(0xfffffff0u, 1) && !gb_state_newer(3, 3));
}

static uint8_t fb[GB_FB_SIZE];

/* run, snapshot, run, restore the snapshot, run again: the same machine state as the first branch */
static void core(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("skip core round trip (no %s, run tools/fetch_roms.sh)\n", path); return; }
    static uint8_t rom[1 << 21];
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    CHECK(gb_core_init(rom, n, fb) == 0);
    gb_core_set_render(false);
    for (int i = 0; i < 300; i++) gb_core_frame(0);
    size_t sz = gb_core_state_size();
    uint8_t *snap = malloc(sz), *a = malloc(sz), *b = malloc(sz);
    for (size_t o = 0; o < sz; o += 1000) gb_core_state_read(o, snap + o, sz - o < 1000 ? sz - o : 1000);
    CHECK(gb_state_rom_id(rom, n) != 0);
    for (int i = 0; i < 200; i++) gb_core_frame(i & 1 ? 0x01 : 0);
    gb_core_state_read(0, a, sz);
    CHECK(memcmp(a, snap, sz) != 0);
    gb_core_state_begin();
    for (size_t o = 0; o < sz; o += 1000) gb_core_state_write(o, snap + o, sz - o < 1000 ? sz - o : 1000);
    CHECK(gb_core_state_end());
    gb_core_state_read(0, b, sz);
    CHECK(memcmp(b, snap, sz) == 0);
    for (int i = 0; i < 200; i++) gb_core_frame(i & 1 ? 0x01 : 0); /* callbacks restored: it still runs */
    gb_core_state_read(0, b, sz);
    CHECK(memcmp(a, b, sz) == 0);
    /* function pointers are never in the payload */
    uint8_t zero[sizeof(void *) * 7] = { 0 };
    CHECK(memcmp(snap, zero, sizeof zero) == 0);
    free(snap); free(a); free(b);
    printf("core state %u bytes\n", (unsigned)sz);
}

int main(void)
{
    headers();
    core("../../tests/roms/libbet.gb");
    printf(fails ? "state test FAILED\n" : "state test ok\n");
    return fails != 0;
}

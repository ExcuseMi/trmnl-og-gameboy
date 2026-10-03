/* Unit test of the save policy and image check. make -C tools/host test. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "gb_save.h"

static int fails, writes;
static uint8_t flash[16 + 64];

static int fake_write(const uint8_t *ram, size_t n)
{
    uint8_t hdr[GB_SAVE_HDR_SIZE];
    writes++;
    memcpy(flash + 16, ram, n);
    gb_save_hdr_make(hdr, ram, n);
    memcpy(flash, hdr, 16);
    return 0;
}
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define S 1000000LL

int main(void)
{
    uint8_t ram[64] = { 0 };
    gb_save_t s;
    gb_save_init(&s, ram, sizeof ram, fake_write, 0);
    CHECK(!gb_save_poll(&s, 100 * S)); /* nothing changed */
    ram[3] = 7;
    gb_save_touch(&s, 100 * S);
    CHECK(!gb_save_poll(&s, 101 * S)); /* not quiet for 2 s yet */
    ram[4] = 8;
    gb_save_touch(&s, 101 * S + 500000);
    CHECK(!gb_save_poll(&s, 103 * S)); /* quiet restarted by the second write */
    CHECK(gb_save_poll(&s, 103 * S + 600000) && writes == 1);
    CHECK(gb_save_hdr_ok(flash, flash + 16, 64) && flash[16 + 3] == 7);
    /* a change within 30 s of the last save waits for the gap (last write at 103.6 s) */
    ram[5] = 9;
    gb_save_touch(&s, 105 * S);
    CHECK(!gb_save_poll(&s, 108 * S) && writes == 1); /* quiet 3 s, gap 4.4 s */
    CHECK(!gb_save_poll(&s, 133 * S) && writes == 1); /* gap 29.4 s */
    CHECK(gb_save_poll(&s, 133700000LL) && writes == 2);
    /* a write of the same value: dirty but the CRC is equal, no flash write */
    gb_save_touch(&s, 140 * S);
    CHECK(!gb_save_poll(&s, 144 * S) && writes == 2);
    /* round trip: a new boot loads what was written */
    uint8_t ram2[64];
    memcpy(ram2, flash + 16, 64);
    CHECK(gb_save_hdr_ok(flash, ram2, 64) && ram2[3] == 7 && ram2[5] == 9);
    /* torn or foreign images are rejected */
    ram2[10] ^= 1;
    CHECK(!gb_save_hdr_ok(flash, ram2, 64));
    memset(flash, 0xff, 16);
    CHECK(!gb_save_hdr_ok(flash, flash + 16, 64));
    CHECK(gb_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926u);
    /* changed sectors: only sectors whose image bytes differ from flash are written */
    {
        enum { N = 3 * 4096 + 100 }; /* 4 sectors, the last one short */
        static uint8_t d[N], img[16 + N + 4096];
        uint8_t h[GB_SAVE_HDR_SIZE];
        memset(img, 0xff, sizeof img);
        for (int i = 0; i < N; i++) d[i] = (uint8_t)(i * 7);
        gb_save_hdr_make(h, d, N);
        memcpy(img, h, 16);
        memcpy(img + 16, d, N);
        CHECK(gb_save_sectors(N) == 4 && gb_save_sectors(4096 - 16) == 1 && gb_save_sectors(4096 - 15) == 2);
        int diff = 0;
        for (size_t i = 0; i < 4; i++) { /* flash holds the same image: nothing to write */
            size_t n = i == 3 ? 16 + N - 3 * 4096 : 4096;
            diff += !gb_save_image_eq(h, d, N, i * 4096, img + i * 4096, n);
        }
        CHECK(diff == 0);
        d[4096 + 5] ^= 1; /* data byte in sector 1 (image offset 4117) */
        d[N - 1] ^= 1;    /* last data byte, sector 3 */
        gb_save_hdr_make(h, d, N);
        uint8_t dif[4] = { 0 };
        for (size_t i = 0; i < 4; i++) {
            size_t n = i == 3 ? 16 + N - 3 * 4096 : 4096;
            dif[i] = !gb_save_image_eq(h, d, N, i * 4096, img + i * 4096, n);
        }
        CHECK(dif[0] && dif[1] && !dif[2] && dif[3]); /* sector 0: the header changed with the CRC */
    }
    printf(fails ? "save test FAILED\n" : "save test ok\n");
    return fails != 0;
}

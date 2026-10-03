/* Host check: run a ROM for N frames with the same core and dither as the firmware, write PNG snapshots.
 *   gb_host <rom.gb> <prefix> [frames=600] [snapshot frames...]   (default 60 300 600)
 * Presses START for frames 100..110 to leave a title screen. PNG: 800x480, 1-bit gray, stored deflate.
 * C11. SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gb_core.h"
#include "gb_render.h"

static uint8_t fb[GB_FB_SIZE];

static uint32_t crc_tab[256];
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *t, const uint8_t *d, uint32_t n)
{
    uint8_t h[8], c[4];
    put32(h, n);
    memcpy(h + 4, t, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    put32(c, ~crc(crc(~0u, (const uint8_t *)t, 4), d, n));
    fwrite(c, 1, 4, f);
}

static void write_png(const char *path)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_tab[i] = c;
    }
    size_t raw = (size_t)(GB_STRIDE + 1) * GB_PANEL_H, nblk = (raw + 65534) / 65535;
    uint8_t *z = malloc(raw + nblk * 5 + 6), *src = malloc(raw);
    for (int y = 0; y < GB_PANEL_H; y++) {
        src[(size_t)y * (GB_STRIDE + 1)] = 0;
        memcpy(src + (size_t)y * (GB_STRIDE + 1) + 1, fb + (size_t)y * GB_STRIDE, GB_STRIDE);
    }
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    for (size_t b = 0, off = 0; b < nblk; b++) {
        size_t n = raw - off > 65535 ? 65535 : raw - off;
        z[o++] = b == nblk - 1; z[o++] = n; z[o++] = n >> 8; z[o++] = ~n; z[o++] = ~n >> 8;
        memcpy(z + o, src + off, n);
        o += n; off += n;
    }
    uint32_t a = 1, s = 0;
    for (size_t i = 0; i < raw; i++) { a = (a + src[i]) % 65521; s = (s + a) % 65521; }
    put32(z + o, s << 16 | a);
    o += 4;
    FILE *f = fopen(path, "wb");
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    uint8_t ihdr[13];
    put32(ihdr, GB_PANEL_W); put32(ihdr + 4, GB_PANEL_H);
    ihdr[8] = 1; ihdr[9] = 0; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    fwrite(sig, 1, 8, f);
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z); free(src);
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    static uint8_t rom[8 << 20];
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    int frames = argc > 3 ? atoi(argv[3]) : 600;
    int snaps[16] = { 60, 300, 600 }, ns = 3;
    if (argc > 4) for (ns = 0; ns < argc - 4 && ns < 16; ns++) snaps[ns] = atoi(argv[4 + ns]);
    gb_fb_clear(fb);
    int rc = gb_core_init(rom, n, fb);
    if (rc) { fprintf(stderr, "gb_core_init: %d\n", rc); return 1; }
    printf("title '%s', %zu bytes\n", gb_core_title(), n);
    gb_core_set_render(true);
    for (int fr = 1; fr <= frames; fr++) {
        gb_core_frame(fr >= 100 && fr < 110 ? 0x08 : 0);
        for (int i = 0; i < ns; i++)
            if (snaps[i] == fr) {
                char p[512];
                snprintf(p, sizeof p, "%s-%d.png", argv[2], fr);
                write_png(p);
                printf("wrote %s\n", p);
            }
    }
    return 0;
}

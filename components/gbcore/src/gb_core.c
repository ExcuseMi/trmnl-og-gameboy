/* See gb_core.h. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define PEANUT_GB_12_COLOUR 0
#define PEANUT_GB_HIGH_LCD_ACCURACY 0
#include "peanut_gb.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "gb_core.h"
#include "gb_render.h"

static struct gb_s gb;
static const uint8_t *g_rom;
static uint8_t *g_ram;
static uint8_t *g_fb;
static bool g_render;
static char g_title[17];
static size_t g_ram_len;
static uint32_t g_ram_writes;

static uint8_t rom_read(struct gb_s *g, const uint_fast32_t a) { (void)g; return g_rom[a]; }
static uint8_t ram_read(struct gb_s *g, const uint_fast32_t a) { (void)g; return g_ram[a]; }
static void ram_write(struct gb_s *g, const uint_fast32_t a, const uint8_t v) { (void)g; g_ram[a] = v; g_ram_writes++; }
static void on_error(struct gb_s *g, const enum gb_error_e e, const uint16_t a) { (void)g; (void)e; (void)a; }

static void draw_line(struct gb_s *g, const uint8_t *px, const uint_fast8_t ly)
{
    (void)g;
    if (g_render && ly < 144) gb_render_line(g_fb, px, ly);
}

int gb_core_init(const uint8_t *rom, size_t len, uint8_t *fb)
{
    if (len < 0x150) return -1;
    g_rom = rom;
    g_fb = fb;
    if (gb_init(&gb, rom_read, ram_read, ram_write, on_error, NULL) != GB_INIT_NO_ERROR) return -2;
    size_t ram = 0;
    if (gb_get_save_size_s(&gb, &ram) != 0) return -3;
    free(g_ram);
    g_ram_len = ram;
    g_ram = calloc(1, ram ? ram : 1);
    if (!g_ram) return -4;
    gb_init_lcd(&gb, draw_line);
    gb_get_rom_name(&gb, g_title);
    return 0;
}

void gb_core_set_render(bool on) { g_render = on; }
void gb_core_frame(uint8_t pressed) { gb.direct.joypad = (uint8_t)~pressed; gb_run_frame(&gb); }
const char *gb_core_title(void) { return g_title; }
uint8_t *gb_core_cart_ram(size_t *len)
{
    *len = g_ram_len;
    return g_ram_len ? g_ram : NULL;
}
uint32_t gb_core_ram_writes(void) { return g_ram_writes; }

/* Pointers kept apart from the payload: the first block of struct gb_s (all callbacks), lcd_draw_line and priv. */
#define PTR_END (offsetof(struct gb_s, gb_bootrom_read) + sizeof gb.gb_bootrom_read)
#define STATE_MAX_SLOT 0xD000u /* partitions.csv: two slots in `state` */
_Static_assert(sizeof(struct gb_s) + 32768 + 32 <= STATE_MAX_SLOT, "state slot too small for gb_s + 32 KB cart RAM");

static struct {
    uint8_t mbc, cart_ram, num_ram_banks;
    uint16_t num_rom_banks_mask;
    void *priv;
    void (*draw)(struct gb_s *, const uint8_t *, const uint_fast8_t);
    uint8_t cb[PTR_END];
} keep;

size_t gb_core_state_size(void) { return sizeof gb + g_ram_len; }

static void zero_part(uint8_t *buf, size_t off, size_t n, size_t f_off, size_t f_len)
{
    size_t a = off > f_off ? off : f_off, b = off + n < f_off + f_len ? off + n : f_off + f_len;
    if (a < b) memset(buf + (a - off), 0, b - a);
}

void gb_core_state_read(size_t off, uint8_t *buf, size_t n)
{
    size_t k = 0;
    if (off < sizeof gb) {
        k = sizeof gb - off < n ? sizeof gb - off : n;
        memcpy(buf, (const uint8_t *)&gb + off, k);
        zero_part(buf, off, k, 0, PTR_END);
        zero_part(buf, off, k, offsetof(struct gb_s, display.lcd_draw_line), sizeof gb.display.lcd_draw_line);
        zero_part(buf, off, k, offsetof(struct gb_s, direct.priv), sizeof gb.direct.priv);
    }
    if (k < n) memcpy(buf + k, g_ram + (off + k - sizeof gb), n - k);
}

void gb_core_state_begin(void)
{
    keep.mbc = (uint8_t)gb.mbc;
    keep.cart_ram = gb.cart_ram;
    keep.num_ram_banks = gb.num_ram_banks;
    keep.num_rom_banks_mask = gb.num_rom_banks_mask;
    keep.priv = gb.direct.priv;
    keep.draw = gb.display.lcd_draw_line;
    memcpy(keep.cb, &gb, PTR_END);
}

void gb_core_state_write(size_t off, const uint8_t *buf, size_t n)
{
    size_t k = 0;
    if (off < sizeof gb) {
        k = sizeof gb - off < n ? sizeof gb - off : n;
        memcpy((uint8_t *)&gb + off, buf, k);
    }
    if (k < n) memcpy(g_ram + (off + k - sizeof gb), buf + k, n - k);
}

bool gb_core_state_end(void)
{
    memcpy(&gb, keep.cb, PTR_END);
    gb.display.lcd_draw_line = keep.draw;
    gb.direct.priv = keep.priv;
    return keep.mbc == (uint8_t)gb.mbc && keep.cart_ram == gb.cart_ram && keep.num_ram_banks == gb.num_ram_banks &&
           keep.num_rom_banks_mask == gb.num_rom_banks_mask;
}

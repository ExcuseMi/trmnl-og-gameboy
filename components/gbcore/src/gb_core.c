/* See gb_core.h. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define PEANUT_GB_12_COLOUR 0
#define PEANUT_GB_HIGH_LCD_ACCURACY 0
#include "peanut_gb.h"
#include <stdlib.h>
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

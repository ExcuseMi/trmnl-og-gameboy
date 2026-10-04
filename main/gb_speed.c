/* See gb_speed.h. C11. SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_speed.h"

/* Faster rows drive each changed pixel shorter (paler, more ghosting). The automatic full refresh is rare on purpose
 * (it flashes the whole picture, seen on hardware as disturbing every 20 s at preset 3); Y on the gamepad cleans up
 * on demand. Frame rates are UC8179 PLL steps (epd_lut.h); 200 Hz is the datasheet maximum. */
static const gb_speed_t SPEED[GB_SPEED_N] = {
    { 0, 0, false, 0 },      /* 0: as before: GxEPD2 waveform 1.4 s, power on and off around every refresh */
    { 0, 0, true, 600 },     /* 1: same waveform, power held: saves the PON and POF waits */
    { 20, 50, true, 600 },   /* 2: 20 frames at 50 Hz = 400 ms, close to a full swing of the pixel */
    { 10, 50, true, 600 },    /* 3: 10 frames at 50 Hz = 200 ms, dark gray instead of black */
    { 6, 100, true, 600 },    /* 4: 6 frames at 100 Hz = 60 ms, pale and noisy */
    { 4, 200, true, 600 },    /* 5: 4 frames at 200 Hz = 20 ms, as fast as the controller goes; expect faint pictures */
};

const gb_speed_t *gb_speed(int n)
{
    return &SPEED[n < 0 ? 0 : n >= GB_SPEED_N ? GB_SPEED_N - 1 : n];
}

unsigned gb_speed_wave_ms(const gb_speed_t *s)
{
    return (s->frames ? s->frames : GB_SPEED_GX_FRAMES) * 1000u / (s->hz ? s->hz : 50);
}

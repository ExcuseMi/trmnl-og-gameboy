/* gb_input: the OG's single button (GPIO2, active low): press = A, hold GB_START_HOLD_MS = Start, or'ed with the
 * BLE gamepad (components/pad). C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_input.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "pad.h"
#include "sdkconfig.h"

static int64_t down_us = -1;

void gb_input_init(void)
{
    gpio_config_t c = { .pin_bit_mask = 1ULL << BOARD_BUTTON, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&c);
#ifdef CONFIG_GB_PAD_BLE
    pad_init();
#endif
}

uint8_t gb_input_poll(void)
{
    if (gpio_get_level(BOARD_BUTTON)) {
        down_us = -1;
        return pad_buttons();
    }
    int64_t now = esp_timer_get_time();
    if (down_us < 0) down_us = now;
    return (now - down_us >= CONFIG_GB_START_HOLD_MS * 1000LL ? 0x08 : 0x01) | pad_buttons();
}

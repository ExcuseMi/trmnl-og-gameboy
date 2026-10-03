# OG panel driver, copied from tiny-paper (see SOURCE.md). SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS esp/epd_uc8179.c src/epd_lut.c src/epd_gray.c src/epd_rects.c
                       INCLUDE_DIRS include ../../include
                       REQUIRES esp_driver_spi esp_driver_gpio esp_timer)

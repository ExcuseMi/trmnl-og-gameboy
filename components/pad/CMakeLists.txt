# Xbox Wireless Controller (BLE): report parser and mapping to Game Boy buttons (src, hardware free: also built by
# tools/host) and the NimBLE central that delivers the reports (esp).
# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS src/xbox_report.c esp/pad_ble.c INCLUDE_DIRS include
                       PRIV_REQUIRES bt nvs_flash esp_timer)

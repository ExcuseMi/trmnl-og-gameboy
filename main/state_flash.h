/* Save state slots in the 'state' partition (format: gb_state.h). C11. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef STATE_FLASH_H
#define STATE_FLASH_H
#include <stdbool.h>
#include <stdint.h>
/* Open the partition for a ROM (rom_id: gb_state_rom_id). False if it is missing or too small for two slots. */
bool state_flash_open(uint32_t rom_id);
/* Resume from the newest valid slot into the core (gb_core_state_*). 1 = resumed, 0 = nothing loaded (start normally),
 * -1 = the core is half loaded and must be reinitialised. */
int state_flash_resume(void);
/* Snapshot the core into the older slot. Pauses the caller for the erase and write. 0 = ok. */
int state_flash_write(void);
#endif

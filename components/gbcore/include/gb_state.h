/* Save state slots: a full emulator snapshot in two alternating flash slots. Slot = [32 byte header][payload]; the
 * payload is written first, the header last, so a power loss leaves the other slot (or an invalid header) behind.
 * Header (little endian words): magic, version, sequence, payload size, ROM identity, payload CRC32, reserved,
 * CRC32 of the first 28 bytes. Hardware free (flash access is in main/state_flash.c). C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GB_STATE_H
#define GB_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GB_STATE_HDR_SIZE 32
#define GB_STATE_VERSION 1 /* bump when the payload layout changes (a struct gb_s change does it by its size) */
#define GB_STATE_SECTOR 4096

enum { GB_STATE_OK, GB_STATE_EMPTY, GB_STATE_BAD_HDR, GB_STATE_BAD_VERSION, GB_STATE_BAD_SIZE, GB_STATE_BAD_ROM };

typedef struct {
    int n;               /* header-valid slots for this ROM and payload size */
    int order[2];        /* slot numbers, newest first */
    uint32_t seq[2], crc[2]; /* by slot number */
    int why[2];          /* GB_STATE_* by slot number */
} gb_state_pick_t;

/* CRC32 (zlib), chained: crc = update(update(0, a), b). */
uint32_t gb_crc32_update(uint32_t crc, const uint8_t *p, size_t n);
/* CRC32 of ROM bytes 0x100..0x14F (header with title, type, sizes and the global checksum). */
uint32_t gb_state_rom_id(const uint8_t *rom, size_t len);
/* Slot bytes for a payload: header + payload rounded up to sectors. */
size_t gb_state_slot_size(size_t payload);
void gb_state_hdr_make(uint8_t hdr[GB_STATE_HDR_SIZE], uint32_t seq, size_t size, uint32_t rom_id, uint32_t payload_crc);
/* GB_STATE_OK or the reason the header is refused; seq and crc (may be NULL) are filled when OK. */
int gb_state_hdr_check(const uint8_t hdr[GB_STATE_HDR_SIZE], uint32_t rom_id, size_t size, uint32_t *seq, uint32_t *crc);
const char *gb_state_why(int why);
/* a is newer than b (sequence numbers wrap). */
bool gb_state_newer(uint32_t a, uint32_t b);
/* Check both headers. The caller verifies the payload CRC of order[0], then order[1]. */
void gb_state_pick(const uint8_t hdr0[GB_STATE_HDR_SIZE], const uint8_t hdr1[GB_STATE_HDR_SIZE], uint32_t rom_id,
                   size_t size, gb_state_pick_t *p);
/* Slot and sequence number for the next write: the slot that does not hold the newest intact header. */
void gb_state_next(const uint8_t hdr0[GB_STATE_HDR_SIZE], const uint8_t hdr1[GB_STATE_HDR_SIZE], int *slot,
                   uint32_t *seq);
#endif

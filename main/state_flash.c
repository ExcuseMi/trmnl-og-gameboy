/* Two alternating slots in the 'state' partition, each [32 byte header][payload]. The payload streams from the core
 * through a small buffer (no second copy of the state in RAM) into an erased slot, the header goes last. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "state_flash.h"
#include <stdio.h>
#include <string.h>
#include "esp_partition.h"
#include "esp_timer.h"
#include "gb_core.h"
#include "gb_state.h"

#define CHUNK 1024

static const esp_partition_t *part;
static size_t slot_size, payload;
static uint32_t rom_id;
static int last_slot = -1; /* slot holding the newest known good state; the next write goes to the other */
static uint32_t last_seq;
static uint8_t buf[CHUNK], cmp[CHUNK];

bool state_flash_open(uint32_t id)
{
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "state");
    payload = gb_core_state_size();
    slot_size = gb_state_slot_size(payload);
    rom_id = id;
    if (!part || 2 * slot_size > part->size) {
        printf("gb state=none (partition %s, need %u)\n", part ? "too small" : "missing", (unsigned)(2 * slot_size));
        part = NULL;
        return false;
    }
    return true;
}

static bool read_hdr(int slot, uint8_t hdr[GB_STATE_HDR_SIZE])
{
    return !esp_partition_read(part, (size_t)slot * slot_size, hdr, GB_STATE_HDR_SIZE);
}

/* CRC of the payload as stored; with load the bytes also go into the core. */
static bool payload_crc(int slot, uint32_t *crc, bool load)
{
    uint32_t c = 0;
    for (size_t off = 0; off < payload; off += CHUNK) {
        size_t n = payload - off < CHUNK ? payload - off : CHUNK;
        if (esp_partition_read(part, (size_t)slot * slot_size + GB_STATE_HDR_SIZE + off, buf, n)) return false;
        c = gb_crc32_update(c, buf, n);
        if (load) gb_core_state_write(off, buf, n);
    }
    *crc = c;
    return true;
}

int state_flash_resume(void)
{
    if (!part) return 0;
    uint8_t h[2][GB_STATE_HDR_SIZE];
    for (int i = 0; i < 2; i++)
        if (!read_hdr(i, h[i])) {
            printf("gb state=none (read failed)\n");
            return 0;
        }
    gb_state_pick_t p;
    gb_state_pick(h[0], h[1], rom_id, payload, &p);
    for (int i = 0; i < 2; i++)
        if (p.why[i] != GB_STATE_OK && p.why[i] != GB_STATE_EMPTY)
            printf("gb state=refused slot=%d why=%s\n", i, gb_state_why(p.why[i]));
    if (p.n == 0) {
        gb_state_next(h[0], h[1], &last_slot, &last_seq);
        last_slot = 1 - last_slot; /* so that the first write goes to the slot gb_state_next chose */
        last_seq--;
        return 0;
    }
    for (int k = 0; k < p.n; k++) {
        int s = p.order[k];
        uint32_t c;
        if (!payload_crc(s, &c, false) || c != p.crc[s]) {
            printf("gb state=refused slot=%d why=payload CRC\n", s);
            continue;
        }
        gb_core_state_begin();
        payload_crc(s, &c, true);
        if (!gb_core_state_end()) {
            printf("gb state=refused slot=%d why=cart mismatch\n", s);
            return -1; /* the core is half loaded: the caller reinitialises it */
        }
        last_slot = s;
        last_seq = p.seq[s];
        printf("gb state=resumed slot=%d seq=%u\n", s, (unsigned)p.seq[s]);
        return 1;
    }
    gb_state_next(h[0], h[1], &last_slot, &last_seq);
    last_slot = 1 - last_slot;
    last_seq--;
    return 0;
}

/* Bytes [off, off+n) of a slot image: header, then the payload. */
static void image(const uint8_t *hdr, size_t off, uint8_t *out, size_t n)
{
    size_t k = 0;
    if (off < GB_STATE_HDR_SIZE) {
        k = GB_STATE_HDR_SIZE - off < n ? GB_STATE_HDR_SIZE - off : n;
        memcpy(out, hdr + off, k);
    }
    if (k < n) gb_core_state_read(off + k - GB_STATE_HDR_SIZE, out + k, n - k);
}

/* Sector i of the slot image: erase and write it unless flash already holds it (the RAM part mostly does not change).
 * 1 = written, 0 = equal, <0 error. */
static int sync_sector(size_t base, size_t i, const uint8_t *hdr)
{
    size_t total = GB_STATE_HDR_SIZE + payload, off = i * GB_STATE_SECTOR;
    size_t n = total - off < GB_STATE_SECTOR ? total - off : GB_STATE_SECTOR;
    bool same = true;
    for (size_t o = 0; same && o < n; o += CHUNK) {
        size_t c = n - o < CHUNK ? n - o : CHUNK;
        if (esp_partition_read(part, base + off + o, cmp, c)) return -1;
        image(hdr, off + o, buf, c);
        same = !memcmp(buf, cmp, c);
    }
    if (same) return 0;
    int rc = esp_partition_erase_range(part, base + off, GB_STATE_SECTOR);
    for (size_t o = 0; !rc && o < n; o += CHUNK) {
        size_t c = n - o < CHUNK ? n - o : CHUNK;
        image(hdr, off + o, buf, c);
        rc = esp_partition_write(part, base + off + o, buf, c);
    }
    return rc ? -2 : 1;
}

int state_flash_write(void)
{
    if (!part) return -1;
    int64_t t0 = esp_timer_get_time();
    int slot = last_slot < 0 ? 0 : 1 - last_slot;
    uint32_t seq = last_slot < 0 ? 1 : last_seq + 1;
    size_t base = (size_t)slot * slot_size;
    uint32_t c = 0;
    for (size_t off = 0; off < payload; off += CHUNK) {
        size_t n = payload - off < CHUNK ? payload - off : CHUNK;
        gb_core_state_read(off, buf, n);
        c = gb_crc32_update(c, buf, n);
    }
    uint8_t hdr[GB_STATE_HDR_SIZE];
    gb_state_hdr_make(hdr, seq, payload, rom_id, c);
    /* sector 0 (with the header) last: until then the old header's CRC fails on the mixed payload and the slot is
     * refused at boot, the other slot stays valid */
    size_t ns = slot_size / GB_STATE_SECTOR, done = 0;
    int rc = 0;
    for (size_t k = 1; k <= ns && !rc; k++) {
        int r = sync_sector(base, k % ns, hdr);
        if (r < 0) rc = r;
        else done += (size_t)r;
    }
    if (rc) {
        printf("gb state=write failed slot=%d rc=%d\n", slot, rc);
        return rc;
    }
    last_slot = slot;
    last_seq = seq;
    printf("gb state=written slot=%d bytes=%u sectors=%u ms=%u\n", slot, (unsigned)payload, (unsigned)done,
           (unsigned)((esp_timer_get_time() - t0) / 1000));
    return 0;
}

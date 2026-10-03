/* Cart RAM in the 'save' partition: [16 byte header][data]. The data is written first, the header last, so a power
 * loss during a save leaves the previous header (or none) and the CRC check rejects a torn image. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "save_flash.h"
#include <stdio.h>
#include "esp_partition.h"
#include "esp_timer.h"
#include "gb_save.h"

static const esp_partition_t *part;

bool save_flash_load(uint8_t *ram, size_t size)
{
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "save");
    if (!part || size == 0 || GB_SAVE_HDR_SIZE + size > part->size) {
        printf("gb save=none (partition %s, ram %u)\n", part ? "too small" : "missing", (unsigned)size);
        part = NULL;
        return false;
    }
    uint8_t hdr[GB_SAVE_HDR_SIZE];
    if (esp_partition_read(part, 0, hdr, sizeof hdr) || esp_partition_read(part, GB_SAVE_HDR_SIZE, ram, size) ||
        !gb_save_hdr_ok(hdr, ram, size)) {
        for (size_t i = 0; i < size; i++) ram[i] = 0;
        printf("gb save=empty\n");
        return false;
    }
    printf("gb save=loaded bytes=%u\n", (unsigned)size);
    return true;
}

/* Sector i of the stored image: erase and write it unless flash already holds it. The data of sector 0 goes first, its
 * header (CRC of all data) last. Returns 1 if written, 0 if equal, <0 on error. */
static int sync_sector(size_t i, const uint8_t *hdr, const uint8_t *ram, size_t size)
{
    size_t total = GB_SAVE_HDR_SIZE + size, off = i * GB_SAVE_SECTOR;
    size_t n = total - off < GB_SAVE_SECTOR ? total - off : GB_SAVE_SECTOR;
    uint8_t buf[128];
    bool same = true;
    for (size_t o = 0; same && o < n; o += sizeof buf) {
        size_t c = n - o < sizeof buf ? n - o : sizeof buf;
        if (esp_partition_read(part, off + o, buf, c)) return -1;
        same = gb_save_image_eq(hdr, ram, size, off + o, buf, c);
    }
    if (same) return 0;
    int rc = esp_partition_erase_range(part, off, GB_SAVE_SECTOR);
    size_t d0 = off < GB_SAVE_HDR_SIZE ? GB_SAVE_HDR_SIZE : off; /* first data byte of this sector in the image */
    if (!rc) rc = esp_partition_write(part, d0, ram + (d0 - GB_SAVE_HDR_SIZE), off + n - d0);
    if (!rc && i == 0) rc = esp_partition_write(part, 0, hdr, GB_SAVE_HDR_SIZE);
    return rc ? -2 : 1;
}

int save_flash_write(const uint8_t *ram, size_t size)
{
    if (!part) return -1;
    int64_t t0 = esp_timer_get_time();
    uint8_t hdr[GB_SAVE_HDR_SIZE];
    gb_save_hdr_make(hdr, ram, size);
    size_t ns = gb_save_sectors(size), done = 0;
    int rc = 0;
    /* sector 0 last: until its header is written the old header's CRC fails on the mixed data, so a torn write is
     * rejected at load (the save reads as empty), never half applied */
    for (size_t k = 1; k <= ns && !rc; k++) {
        int r = sync_sector(k % ns, hdr, ram, size);
        if (r < 0) rc = r;
        else done += (size_t)r;
    }
    if (rc) printf("gb save=write failed rc=%d\n", rc);
    else printf("gb save=written sectors=%u bytes=%u ms=%u\n", (unsigned)done, (unsigned)size,
                (unsigned)((esp_timer_get_time() - t0) / 1000));
    return rc;
}

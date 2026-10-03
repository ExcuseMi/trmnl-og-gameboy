/* Cart RAM in the 'save' partition: [16 byte header][data]. The data is written first, the header last, so a power
 * loss during a save leaves the previous header (or none) and the CRC check rejects a torn image. C11.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "save_flash.h"
#include <stdio.h>
#include "esp_partition.h"
#include "gb_save.h"

static const esp_partition_t *part;

static size_t erase_len(size_t n) { return (n + 4095) & ~(size_t)4095; }

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

int save_flash_write(const uint8_t *ram, size_t size)
{
    if (!part) return -1;
    uint8_t hdr[GB_SAVE_HDR_SIZE];
    gb_save_hdr_make(hdr, ram, size);
    int rc = esp_partition_erase_range(part, 0, erase_len(GB_SAVE_HDR_SIZE + size));
    if (!rc) rc = esp_partition_write(part, GB_SAVE_HDR_SIZE, ram, size);
    if (!rc) rc = esp_partition_write(part, 0, hdr, sizeof hdr);
    printf("gb save=%s bytes=%u\n", rc ? "write failed" : "written", (unsigned)size);
    return rc;
}

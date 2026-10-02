/*
 * flash_wearleveling.c - Flash wear-leveling engine for RT-950 Pro
 *
 * Implements two WL algorithms reverse-engineered from OEM V0.27:
 *
 *   V0.27 OEM functions:
 *     Bitmap scan    @ 0x0800EF68  (find first '1' bit LSB->MSB)
 *     SYSCFG write   @ 0x0800F918  (bitmap WL, 8<<13 = 0x10000 base)
 *     VFOCFG read    @ 0x0800F6C4  (bitmap WL)
 *     EXTCFG R/W     @ 0x0800F600 / 0x0800FAF4  (bitmap WL)
 *     CHCFG write    @ 0x0800F9C8  (marker WL)
 *     VFOSEL write   @ 0x0800FFEC  (bitmap WL)
 *     Channel R/W    @ 0x0800FA58  (32B sequential)
 *     Mask table     @ 0x0802F688  (bitmap mask lookup)
 *
 *   Bitmap-based (SYSCFG, VFOCFG, EXTCFG, VFOSEL):
 *     Bitmap at sector start, 1 bit per slot (1=free, 0=used).
 *     Scan LSB->MSB for first '1' bit = slot with current data.
 *     Read  -> slot[first_free].
 *     Write -> write data at slot[first_free + 1], clear bit[first_free].
 *     Full  -> erase sector, write slot 0 (bitmap left as 0xFF).
 *
 *   Marker-based (CHCFG at 0x11000, dual 4KB sectors):
 *     0xA5 marker byte at slot offset 0 = valid.
 *     Sequential scan to find the one valid slot.
 *     Invalidate old marker (->0x00), write next slot with 0xA5.
 *     Mid-point erase at index 41 (OEM: cmp r4, 0x29).
 *     Wrap-around erase when full.
 *
 *   CRC-16 CCITT: offset 0x7E (SYSCFG records), 101-102 (CHCFG contacts).
 *
 *   Power-loss vulnerability: marker invalidation (write 0x00) occurs
 *   BEFORE new slot write. Power loss between these steps loses data.
 *
 *   Erase: OEM exclusively uses 4K sector erase (cmd 0x20).
 *   32K/64K/chip erase functions exist but have 0 call sites.
 */

#include "drivers/flash_wearleveling.h"
#include "drivers/flash_layout.h"
#include "drivers/spi_flash.h"
#include "drivers/spi.h"

/* Sector Descriptors ------------------------------------------------ */

const wl_sector_t WL_SYSCFG = {
    .flash_addr  = FLASH_ADDR_SYSCFG,
    .sector_size = FLASH_SECTOR_SIZE,
    .record_size = FLASH_SYSCFG_RECORD_SIZE,
    .max_slots   = FLASH_SYSCFG_MAX_SLOTS,
    .data_offset = FLASH_SYSCFG_RECORD_OFFSET,
    .dual_sector = 0,
};

const wl_sector_t WL_VFOCFG = {
    .flash_addr  = FLASH_ADDR_VFOCFG,
    .sector_size = FLASH_SECTOR_SIZE,
    .record_size = 96,
    .max_slots   = FLASH_VFOCFG_MAX_SLOTS,
    .data_offset = 0x0010,  /* OEM: 6-byte bitmap padded to 16 */
    .dual_sector = 0,
};

const wl_sector_t WL_EXTCFG = {
    .flash_addr  = FLASH_ADDR_EXTCFG,
    .sector_size = FLASH_SECTOR_SIZE,
    .record_size = FLASH_EXTCFG_RECORD_SIZE,
    .max_slots   = FLASH_EXTCFG_MAX_SLOTS,
    .data_offset = FLASH_EXTCFG_RECORD_OFFSET,
    .dual_sector = 0,
};

const wl_sector_t WL_VFOSEL = {
    .flash_addr  = FLASH_ADDR_VFOSEL,
    .sector_size = FLASH_SECTOR_SIZE,
    .record_size = FLASH_VFOSEL_RECORD_SIZE,
    .max_slots   = FLASH_VFOSEL_MAX_SLOTS,
    .data_offset = FLASH_VFOSEL_RECORD_OFFSET,
    .dual_sector = 0,
};

const wl_sector_t WL_CHCFG = {
    .flash_addr  = FLASH_ADDR_CONTACTS,
    .sector_size = FLASH_SECTOR_SIZE,
    .record_size = FLASH_CONTACT_RECORD_SIZE - 1,  /* 103 total - 1 marker */
    .max_slots   = FLASH_CONTACT_MAX_ENTRIES,
    .data_offset = 0,
    .dual_sector = 1,
};

/* Internal Helpers -------------------------------------------------- */

static uint16_t bm_size(const wl_sector_t *sec)
{
    return (sec->max_slots + 7u) / 8u;
}

/* Write data to flash handling page-boundary crossing */
static void flash_write_data(uint32_t addr, const uint8_t *data, uint16_t len)
{
    while (len > 0) {
        uint16_t off_in_page = (uint16_t)(addr % FLASH_PAGE_SIZE);
        uint16_t page_remain = (uint16_t)(FLASH_PAGE_SIZE - off_in_page);
        uint16_t chunk = len < page_remain ? len : page_remain;
        spi_flash_write_page(addr, data, chunk);
        addr += chunk;
        data += chunk;
        len  -= chunk;
    }
}

/* Bitmap-Based WL --------------------------------------------------- */

/*
 * Scan bitmap LSB->MSB to find first set bit (first '1').
 *
 * OEM semantics: 1 = slot contains current data (not yet superseded),
 * 0 = slot has been superseded.  After erase-write, all bits are 1 and
 * first_free returns 0 (data at slot 0).  After each normal write the
 * previous slot's bit is cleared and first_free advances.
 *
 * Returns slot index [0..max_slots-1], or -1 if all bits are 0 (full).
 */
static int16_t bitmap_first_free(const wl_sector_t *sec)
{
    uint16_t sz = bm_size(sec);
    uint8_t bm[32];            /* max 254 slots -> 32 bytes */

    spi_flash_read(sec->flash_addr, bm, sz);

    for (uint16_t i = 0; i < sec->max_slots; i++) {
        if (bm[i >> 3] & (uint8_t)(1u << (i & 7u)))
            return (int16_t)i;
    }
    return -1;
}

/*
 * OEM bitmap read: current data lives at slot[first_free].
 * first_free == -1 means sector is full (all bits cleared); the last
 * slot (max_slots - 1) holds the most recent data in that case, though
 * normally the caller should not reach this state.
 */
static int bitmap_read(const wl_sector_t *sec, void *buf)
{
    int16_t first_free = bitmap_first_free(sec);

    uint16_t current;
    if (first_free < 0)
        current = sec->max_slots - 1;  /* all used: last slot */
    else
        current = (uint16_t)first_free;

    uint32_t addr = sec->flash_addr + sec->data_offset
                  + (uint32_t)current * sec->record_size;
    spi_flash_read(addr, (uint8_t *)buf, sec->record_size);
    return 0;
}

/*
 * OEM bitmap write:
 *   Normal: write data to slot[first_free + 1], then clear bit[first_free].
 *   Full:   erase sector, write slot 0 at data_offset.  Bitmap stays 0xFF
 *           (all free), so first_free will be 0 on next read.
 */
static int bitmap_write(const wl_sector_t *sec, const void *buf)
{
    int16_t first_free = bitmap_first_free(sec);

    if (first_free < 0 || (uint16_t)first_free >= sec->max_slots - 1) {
        /* Sector full: erase, write to slot 0, leave bitmap as 0xFF */
        spi_flash_erase_4k(sec->flash_addr);
        flash_write_data(sec->flash_addr + sec->data_offset,
                         (const uint8_t *)buf, sec->record_size);
        return 0;
    }

    /* Write data at the NEXT slot (first_free + 1) */
    uint16_t next_slot = (uint16_t)(first_free + 1);
    uint32_t addr = sec->flash_addr + sec->data_offset
                  + (uint32_t)next_slot * sec->record_size;
    flash_write_data(addr, (const uint8_t *)buf, sec->record_size);

    /* Clear current slot's bit in the bitmap (NOR AND semantics) */
    uint16_t slot = (uint16_t)first_free;
    uint8_t mask = (uint8_t)~(1u << (slot & 7u));
    spi_flash_write_page(sec->flash_addr + (slot >> 3), &mask, 1);

    return 0;
}

/* Marker-Based WL --------------------------------------------------- */

static uint16_t marker_stride(const wl_sector_t *sec)
{
    return (uint16_t)(1u + sec->record_size);
}

/*
 * Scan slots for the one with 0xA5 valid marker.
 * Returns slot index, or -1 if not found.
 */
static int16_t marker_find_valid(const wl_sector_t *sec)
{
    uint16_t stride = marker_stride(sec);
    uint32_t addr = sec->flash_addr;

    for (uint16_t i = 0; i < sec->max_slots; i++) {
        uint8_t m;
        spi_flash_read(addr, &m, 1);
        if (m == WL_VALID_MARKER)
            return (int16_t)i;
        addr += stride;
    }
    return -1;
}

static void marker_write_slot0(const wl_sector_t *sec, const void *buf)
{
    uint8_t m = WL_VALID_MARKER;
    spi_flash_write_page(sec->flash_addr, &m, 1);
    flash_write_data(sec->flash_addr + 1,
                     (const uint8_t *)buf, sec->record_size);
}

static int marker_read(const wl_sector_t *sec, void *buf)
{
    int16_t slot = marker_find_valid(sec);
    if (slot < 0)
        return -1;

    uint32_t addr = sec->flash_addr
                  + (uint32_t)slot * marker_stride(sec) + 1;
    spi_flash_read(addr, (uint8_t *)buf, sec->record_size);
    return 0;
}

static int marker_write(const wl_sector_t *sec, const void *buf)
{
    uint16_t stride = marker_stride(sec);
    int16_t current = marker_find_valid(sec);

    if (current < 0) {
        /* No valid record: erase sector(s) and write slot 0 */
        spi_flash_erase_4k(sec->flash_addr);
        if (sec->dual_sector)
            spi_flash_erase_4k(sec->flash_addr + sec->sector_size);
        marker_write_slot0(sec, buf);
        return 0;
    }

    if (current >= (int16_t)(sec->max_slots - 1)) {
        /* Last slot: erase and wrap to slot 0 */
        if (sec->dual_sector)
            spi_flash_erase_4k(sec->flash_addr + sec->sector_size);
        spi_flash_erase_4k(sec->flash_addr);
        marker_write_slot0(sec, buf);
        return 0;
    }

    /* Invalidate current marker (0xA5 -> 0x00 via NOR AND) */
    uint32_t cur_addr = sec->flash_addr + (uint32_t)current * stride;
    uint8_t zero = 0x00;
    spi_flash_write_page(cur_addr, &zero, 1);

    /* Write new record with marker at next slot */
    uint32_t nxt_addr = sec->flash_addr
                      + (uint32_t)(current + 1) * stride;
    uint8_t m = WL_VALID_MARKER;
    spi_flash_write_page(nxt_addr, &m, 1);
    flash_write_data(nxt_addr + 1, (const uint8_t *)buf, sec->record_size);

    /* Mid-point erase: OEM erases first sector when old slot == 41 */
    if (sec->dual_sector && current == 41)
        spi_flash_erase_4k(sec->flash_addr);

    return 0;
}

/* Public API -------------------------------------------------------- */

void wl_init(const wl_sector_t *sec)
{
    /* No persistent state; scanning is done in read/write.
     * Stub kept for future cache / integrity-check expansion. */
    (void)sec;
}

int wl_read(const wl_sector_t *sec, void *buf)
{
    if (sec->dual_sector)
        return marker_read(sec, buf);
    return bitmap_read(sec, buf);
}

int wl_write(const wl_sector_t *sec, const void *buf)
{
    if (sec->dual_sector)
        return marker_write(sec, buf);
    return bitmap_write(sec, buf);
}

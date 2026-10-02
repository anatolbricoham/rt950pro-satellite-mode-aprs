/*
 * flash_wearleveling.h - Flash wear-leveling engine for RT-950 Pro
 *
 * Two WL algorithms matching OEM firmware behavior:
 *   1. Bitmap-based: SYSCFG, VFOCFG, EXTCFG, VFOSEL sectors
 *   2. Marker-based (0xA5): CHCFG dual-sector (contacts/DTMF)
 *
 * Algorithm selection: dual_sector == 0 -> bitmap, dual_sector == 1 -> marker
 */

#ifndef DRIVERS_FLASH_WEARLEVELING_H
#define DRIVERS_FLASH_WEARLEVELING_H

#include <stdint.h>

/* Wear-leveling sector descriptor */
typedef struct {
    uint32_t flash_addr;        /* Base address of WL sector in SPI flash */
    uint16_t sector_size;       /* Total sector size (usually 4096) */
    uint16_t record_size;       /* Size of each data record (excluding marker) */
    uint16_t max_slots;         /* Max bitmap slots (OEM threshold for full) */
    uint16_t data_offset;       /* Byte offset from sector start to slot 0 data */
    uint8_t  dual_sector;       /* 1 = uses two consecutive sectors (8KB) */
} wl_sector_t;

/* Pre-defined WL sector configurations */
extern const wl_sector_t WL_SYSCFG;     /* System config */
extern const wl_sector_t WL_VFOCFG;     /* VFO config */
extern const wl_sector_t WL_EXTCFG;     /* Extended config */
extern const wl_sector_t WL_VFOSEL;     /* VFO selection state */
extern const wl_sector_t WL_CHCFG;      /* Channel config (marker-based) */

/* Initialize WL engine for a sector (scan to find current slot) */
void wl_init(const wl_sector_t *sector);

/* Read the most recent valid record from a WL sector.
 * Returns 0 on success, -1 if no valid record found. */
int wl_read(const wl_sector_t *sector, void *buf);

/* Write a new record to the WL sector.
 * Handles slot advancement and sector erase when full.
 * Returns 0 on success, -1 on error. */
int wl_write(const wl_sector_t *sector, const void *buf);

#endif /* DRIVERS_FLASH_WEARLEVELING_H */

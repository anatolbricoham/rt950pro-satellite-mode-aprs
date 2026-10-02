/**
 * @file spi_flash.h
 * @brief SPI Flash driver definitions for RT-950 Pro
 *
 * Flash chip: Winbond W25Q16BV (2MB / 16Mbit) or compatible
 * Interface: SPI2 (PB12=CS, PB13=SCK, PB14=MISO, PB15=MOSI)
 * SPI2 base: 0x40003800
 *
 * Reverse-engineered from RT_950Pro_V0.27 firmware.
 */

#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdint.h>
#include <stddef.h>

/* ----------------------------------------------------------------------
 * SPI Flash Hardware
 * -------------------------------------------------------------------- */

#define SPI_FLASH_SPI_BASE          0x40003800  /* SPI2 peripheral */
#define SPI_FLASH_CS_GPIO_BASE      0x40010C00  /* GPIOB */
#define SPI_FLASH_CS_PIN            (1 << 12)   /* PB12 */

#define SPI_FLASH_TOTAL_SIZE        0x200000    /* 2 MB */
#define SPI_FLASH_SECTOR_SIZE       0x1000      /* 4 KB */
#define SPI_FLASH_BLOCK32_SIZE      0x8000      /* 32 KB */
#define SPI_FLASH_BLOCK64_SIZE      0x10000     /* 64 KB */
#define SPI_FLASH_PAGE_SIZE         0x100       /* 256 B */

/* ----------------------------------------------------------------------
 * SPI Flash Commands (Winbond W25Q series)
 * -------------------------------------------------------------------- */

#define FLASH_CMD_READ_DATA         0x03
#define FLASH_CMD_PAGE_PROGRAM      0x02
#define FLASH_CMD_WRITE_ENABLE      0x06
#define FLASH_CMD_READ_STATUS_1     0x05    /* SR1: BUSY, WEL, ... */
#define FLASH_CMD_READ_STATUS_2     0x35    /* SR2 */
#define FLASH_CMD_READ_STATUS_3     0x15    /* SR3 */
#define FLASH_CMD_SECTOR_ERASE_4K   0x20
#define FLASH_CMD_BLOCK_ERASE_32K   0x52
#define FLASH_CMD_BLOCK_ERASE_64K   0xD8
#define FLASH_CMD_CHIP_ERASE        0xC7

/* Status register bits */
#define FLASH_SR1_BUSY              (1 << 0)
#define FLASH_SR1_WEL               (1 << 1)

/* ----------------------------------------------------------------------
 * Flash Memory Map
 *
 *  Address Range        Size     Content
 *  ---------------------------------------------------------------
 *  0x000000-0x007FFF    32 KB    Channel Memory (990 channels)
 *  0x008000-0x008FFF     4 KB    Channel Config (wear-leveled)
 *  0x009000-0x009FFF     4 KB    VFO/Scan Settings (wear-leveled)
 *  0x00A000-0x00AFFF     4 KB    Supplementary Settings + Zone Names
 *  0x00B000-0x00BFFF     4 KB    Extended Radio Settings (wear-leveled)
 *  0x00C000-0x00CFFF     4 KB    (Reserved)
 *  0x00D000-0x00DFFF     4 KB    Zone Channel Map
 *  0x00E000-0x00EFFF     4 KB    VFO/Channel Select (wear-leveled)
 *  0x00F000-0x00FFFF     4 KB    Calibration / Tuning Data
 *  0x010000-0x010FFF     4 KB    System Config (wear-leveled)
 *  0x011000-0x012FFF     8 KB    Contact/DTMF Names (wear-leveled)
 *  0x013000-0x02BFFF   100 KB    DTMF Contacts Extended (25 sectors)
 *  0x02C000-0x08FFFF   400 KB    (Reserved/Unused)
 *  0x090000-0x09FFFF    64 KB    Boot Splash / Logo Image
 *  0x0A0000-0x15BFFF   768 KB    (Reserved/Unused)
 *  0x15C000-0x15CFFF     4 KB    ASCII Font (39 B/glyph)
 *  0x15CF00-0x191FFF   213 KB    CJK Font Small (32 B/glyph)
 *  0x1986C0-0x199FFF     6 KB    CJK Font Tiny (16 B/glyph)
 *  0x199E80-0x1C3FFF   168 KB    CJK Font Medium (24 B/glyph)
 *  0x1C3C40-0x1C40FF     1 KB    ASCII Font Medium (12 B/glyph)
 *  0x1CCF00-0x1D3FFF    29 KB    Large Display Font (988 B/glyph)
 *  0x1D4000-0x1FFFFF   176 KB    (Reserved/Free)
 *  ---------------------------------------------------------------
 * -------------------------------------------------------------------- */

/* === Channel Memory: 0x000000 - 0x007FFF ===
 *
 * 990 channels, 32 bytes each, stored linearly (verified via KDH CPS server).
 * 128 channels per 4KB sector (8 sectors total, 31680B / 32768B used).
 * Writes use read-modify-write on entire sector.
 * OEM also maintains a 100-slot circular log (6B entries) for active
 * channel tracking - see flash_channel_data_read @ 0x0800FC84.
 *
 * Channel record (32 bytes / 0x20):
 *   [0..3]   Frequency in BCD format (4 bytes, MSB first)
 *   [4..14]  Channel parameters (TX freq, CTCSS, power, etc.)
 *   [15]     Flags byte (bit 2 = additional feature flag)
 *   [16..31] Extended parameters
 */
#define FLASH_CHANNEL_BASE          0x000000
#define FLASH_CHANNEL_RECORD_SIZE   0x20        /* 32 bytes */
#define FLASH_CHANNEL_COUNT         990         /* verified via KDH server + OEM refs */
#define FLASH_CHANNEL_PER_SECTOR    128
#define FLASH_CHANNEL_SECTORS       8           /* 0x000000-0x007FFF */
#define FLASH_CHANNEL_END           0x008000

/* === Channel Config: 0x008000 - 0x008FFF ===
 *
 * Wear-leveled storage for active channel configuration.
 * 6-byte allocation bitmap at 0x8000.
 * Up to 40 data slots of 0x62 (98) bytes starting at 0x8010.
 * Records store 3 VFO channel states (0x20 bytes each + CRC).
 */
#define FLASH_CHCFG_BASE            0x008000
#define FLASH_CHCFG_BITMAP_SIZE     6
#define FLASH_CHCFG_RECORD_OFFSET   0x0010
#define FLASH_CHCFG_RECORD_SIZE     0x62        /* 98 bytes */
#define FLASH_CHCFG_MAX_SLOTS       40

/* === VFO/Scan Settings: 0x009000 - 0x009FFF ===
 *
 * Wear-leveled radio settings page (Settings Block B).
 * 6-byte bitmap at 0x9000.
 * Up to 41 records of 96 bytes (OEM verified @ lines 11115-11118).
 * Contains VFO frequencies, scan ranges, step sizes.
 * Stores g_config (32B @ 0x2000A8B0) + g_display (41B @ 0x2000A8D0).
 * CRC at offset 0x5E in each slot.
 *
 * Note: Settings A (0x8000) uses 98B records, Settings B uses 96B.
 */
#define FLASH_VFOCFG_BASE           0x009000
#define FLASH_VFOCFG_BITMAP_SIZE    6
#define FLASH_VFOCFG_RECORD_OFFSET  0x0010      /* 6-byte bitmap padded to 16 */
#define FLASH_VFOCFG_RECORD_SIZE    0x60        /* 96 bytes (OEM confirmed) */
#define FLASH_VFOCFG_MAX_SLOTS      41          /* OEM: 41 (fits 4KB sector) */

/* === Supplementary Settings: 0x00A000 - 0x00AFFF ===
 *
 * 0xA000: 32 bytes - basic radio config (band settings, etc.)
 * 0xA020: 8 bytes  - additional parameters
 * 0xA200: Zone name table (16 entries x 16 bytes, 12 bytes name + padding)
 */
#define FLASH_SUPPL_BASE            0x00A000
#define FLASH_SUPPL_MAIN_SIZE       0x20        /* 32 bytes at 0xA000 */
#define FLASH_SUPPL_AUX_OFFSET      0x0020      /* 8 bytes at 0xA020 */
#define FLASH_SUPPL_AUX_SIZE        0x08
#define FLASH_ZONE_NAME_BASE        0x00A200
#define FLASH_ZONE_NAME_ENTRY_SIZE  0x10        /* 16 bytes per zone */
#define FLASH_ZONE_NAME_LEN         12
#define FLASH_ZONE_MAX_COUNT        16

/* === Extended Radio Settings: 0x00B000 - 0x00BFFF ===
 *
 * Wear-leveled extended settings.
 * 8-byte allocation bitmap at 0xB000.
 * Up to 25 data slots of 0xA0 (160) bytes at 0xB010.
 * CRC-16 at offset 0x9E within each record.
 * Stores: CTCSS/DCS codes, squelch, VOX, display settings.
 */
#define FLASH_EXTCFG_BASE           0x00B000
#define FLASH_EXTCFG_BITMAP_SIZE    8
#define FLASH_EXTCFG_RECORD_OFFSET  0x0010
#define FLASH_EXTCFG_RECORD_SIZE    0xA0        /* 160 bytes */
#define FLASH_EXTCFG_MAX_SLOTS      25
#define FLASH_EXTCFG_CRC_OFFSET     0x9E

/* === Zone Channel Map: 0x00D000 - 0x00DFFF ===
 *
 * Maps zone/channel indices to display names.
 * Entries at 0xD200 + (index+1) x 16, 12 bytes per entry.
 */
#define FLASH_ZONEMAP_BASE          0x00D000
#define FLASH_ZONEMAP_DATA_OFFSET   0x0200
#define FLASH_ZONEMAP_ENTRY_SIZE    0x10
#define FLASH_ZONEMAP_NAME_LEN      12

/* === VFO/Channel Select: 0x00E000 - 0x00EFFF ===
 *
 * Wear-leveled VFO state and channel selection.
 * 32-byte bitmap at 0xE000 for allocation tracking.
 * Up to 254 records of 16 bytes at 0xE020.
 * CRC-16 at offset 0x0E.
 * Stores: current VFO frequencies, active channel numbers.
 */
#define FLASH_VFOSEL_BASE           0x00E000
#define FLASH_VFOSEL_BITMAP_SIZE    0x20
#define FLASH_VFOSEL_RECORD_OFFSET  0x0020
#define FLASH_VFOSEL_RECORD_SIZE    0x10        /* 16 bytes */
#define FLASH_VFOSEL_MAX_SLOTS      254
#define FLASH_VFOSEL_CRC_OFFSET     0x0E

/* === Calibration / Tuning Data: 0x00F000 - 0x00F2FF ===
 *
 * Fixed-address calibration values (NOT wear-leveled).
 * Flag byte at 0xF0E0 indicates if calibration is programmed (!=0xFF).
 *
 * Layout:
 *   0xF000-0xF0BF: 12 x 16-byte VFO frequency limit blocks
 *                   (loaded to 0x20000014-0x200000C4)
 *   0xF0C0-0xF0C9: 10 bytes - extra cal A
 *   0xF0D0-0xF0D9: 10 bytes - extra cal B
 *   0xF0E0-0xF0EF: 16 bytes - flag byte + system config
 *   0xF0F0-0xF0FF: 16 bytes - additional config
 *   0xF100-0xF11F: 2 x 16-byte tuning tables
 *   0xF170-0xF18F: 32 bytes - tuning/offset table
 *   0xF190-0xF1BF: 3 x 16-byte extended tuning
 *   0xF200-0xF206: 7 bytes  - boot/startup config
 *   0xF210-0xF21A: 11 bytes - DTMF/signaling config
 *   0xF230-0xF24F: 2 x 16-byte config blocks (if not in RAM)
 */
#define FLASH_CAL_BASE              0x00F000
#define FLASH_CAL_VFO_LIMITS        0x00F000    /* 12 x 16 bytes */
#define FLASH_CAL_EXTRA_A           0x00F0C0    /* 10 bytes */
#define FLASH_CAL_EXTRA_B           0x00F0D0    /* 10 bytes */
#define FLASH_CAL_FLAG              0x00F0E0    /* Flag + 16 bytes */
#define FLASH_CAL_ADDITIONAL        0x00F0F0    /* 16 bytes */
#define FLASH_CAL_TUNING_1          0x00F100    /* 2 x 16 bytes */
#define FLASH_CAL_TUNING_2          0x00F170    /* 32 bytes */
#define FLASH_CAL_TUNING_3          0x00F190    /* 3 x 16 bytes */
#define FLASH_CAL_BOOT_CFG          0x00F200    /* 7 bytes */
#define FLASH_CAL_DTMF_CFG          0x00F210    /* 11 bytes */
#define FLASH_CAL_END               0x00F300

/* === System Config: 0x010000 - 0x010FFF ===
 *
 * Wear-leveled system configuration.
 * 8-byte bitmap at 0x10000 for slot allocation.
 * Up to 30 records of 0x80 (128) bytes starting at 0x10008.
 * Data payload: 0x77 (119) bytes + CRC-16 at end.
 * Stores: radio model, firmware version, feature flags.
 */
#define FLASH_SYSCFG_BASE           0x010000
#define FLASH_SYSCFG_BITMAP_SIZE    8
#define FLASH_SYSCFG_RECORD_OFFSET  0x0008
#define FLASH_SYSCFG_RECORD_SIZE    0x80        /* 128 bytes per slot */
#define FLASH_SYSCFG_DATA_SIZE      0x79        /* 121 bytes payload (V0.27 @ 0x0800F918) */
#define FLASH_SYSCFG_MAX_SLOTS      30

/* === Contact/DTMF Names: 0x011000 - 0x012FFF ===
 *
 * Wear-leveled contact name storage.
 * First byte 0xA5 marks valid entry.
 * Up to 79 entries x 0x67 (103) bytes.
 * CRC-16 at offset 5, data at offset 7 (0x65 = 101 bytes).
 * Stores: DTMF contact names and codes.
 */
#define FLASH_CONTACT_BASE          0x011000
#define FLASH_CONTACT_RECORD_SIZE   0x67        /* 103 bytes */
#define FLASH_CONTACT_VALID_MARKER  0xA5
#define FLASH_CONTACT_MAX_ENTRIES   79
#define FLASH_CONTACT_DATA_OFFSET   7
#define FLASH_CONTACT_DATA_SIZE     0x65        /* 101 bytes */

/* === DTMF Contacts Extended: 0x013000 - 0x02BFFF ===
 *
 * 25 sectors, each holding up to 4 contacts.
 * Total capacity: 100 DTMF contacts.
 * Per-sector layout:
 *   [0x00-0x97]  Wear-leveling header (19 entries x 8 bytes)
 *   [0xA0+n*0xCD] Contact data (4 x 205-byte blocks)
 * First byte 0xA5 marks valid wear-leveling entry.
 */
#define FLASH_DTMF_EXT_BASE        0x013000
#define FLASH_DTMF_EXT_SECTORS     25
#define FLASH_DTMF_EXT_END         0x02C000
#define FLASH_DTMF_CONTACTS_PER_SECTOR  4
#define FLASH_DTMF_TOTAL_CONTACTS  100
#define FLASH_DTMF_HEADER_ENTRIES  19
#define FLASH_DTMF_DATA_OFFSET     0xA0
#define FLASH_DTMF_RECORD_SIZE     0xCD        /* 205 bytes */

/* === Boot Splash / Logo: 0x090000 ===
 *
 * Boot logo image, read in 0x3C00-byte blocks.
 * Displayed during power-on. Multiple blocks for full screen.
 */
#define FLASH_SPLASH_BASE           0x090000
#define FLASH_SPLASH_BLOCK_SIZE     0x3C00      /* 15360 bytes per block */

/* === Font Tables ===
 *
 * Multiple font tables for different display sizes.
 * Read-only data programmed at factory.
 */
#define FLASH_FONT_ASCII_LARGE      0x15C000    /* 95 chars x 39 B = 3705 B V0.27 @ 0x08025826 */
#define FLASH_FONT_ASCII_LARGE_GPW  39          /* glyph width in bytes */

#define FLASH_FONT_CJK_SMALL        0x15CF00    /* CJK chars x 32 B */
#define FLASH_FONT_CJK_SMALL_GPW    32

#define FLASH_FONT_CJK_TINY         0x1986C0    /* CJK chars x 16 B */
#define FLASH_FONT_CJK_TINY_GPW     16

#define FLASH_FONT_CJK_MEDIUM       0x199E80    /* CJK chars x 24 B */
#define FLASH_FONT_CJK_MEDIUM_GPW   24

#define FLASH_FONT_ASCII_MEDIUM      0x1C3C40   /* 95 chars x 12 B = 1140 B */
#define FLASH_FONT_ASCII_MEDIUM_GPW  12

#define FLASH_FONT_DISPLAY_PRIMARY   0x1D35C8   /* Display glyphs, 192 B each V0.27 lit @ 0x080147C8 */
#define FLASH_FONT_DISPLAY_ALT       0x1D4090   /* Alternate display V0.27 lit @ 0x080147CC */
#define FLASH_FONT_DISPLAY_GPW       192

/* ----------------------------------------------------------------------
 * OEM Flash Driver Function Addresses (V0.27 binary)
 *
 * Verified by disassembly of RT_950Pro_V0.27_decrypted.bin.
 * Binary loaded: r2 -m 0x08000000 (correct AT32F403A flash base).
 *
 * CORRECTION (6J): All prior addresses were +0x3000 too high due to
 * earlier analysis using wrong r2 base (-m 0x08003000). Every address
 * below has been re-verified at the corrected offset.
 * -------------------------------------------------------------------- */

/* Low-level SPI flash driver (all verified via disassembly) */
#define OEM_SPI2_INIT               0x08014000  /* SPI2 base 0x40003800, Mode 3 */
#define OEM_FLASH_WRITE_ENABLE      0x08021278  /* cmd 0x06 with CS toggle */
#define OEM_FLASH_SEND_BYTE         0x08021130  /* SPI2 full-duplex xfer */
#define OEM_FLASH_RECV_BYTE         0x08021028  /* SPI2 RX (dummy TX) */
#define OEM_FLASH_READ_STATUS       0x080210E4  /* arg: 1=SR1, 2=SR2, 3=SR3 */
#define OEM_FLASH_IS_BUSY           0x08020E68  /* SR1 bit0 (WIP) check */
#define OEM_FLASH_READ              0x0802107C  /* cmd 0x03, (addr, buf, len) */
#define OEM_FLASH_PAGE_PROGRAM      0x0802118C  /* cmd 0x02, WREN+busy-wait */
#define OEM_FLASH_WRITE             0x08021210  /* multi-page wrapper (256B pages) */
#define OEM_FLASH_SECTOR_ERASE_4K   0x08020FBC  /* cmd 0x20 - ONLY erase used */
#define OEM_FLASH_BLOCK_ERASE_32K   0x08020E7C  /* cmd 0x52 - NEVER called */
#define OEM_FLASH_BLOCK_ERASE_64K   0x08020EEC  /* cmd 0xD8 - NEVER called */
#define OEM_FLASH_CHIP_ERASE        0x08020F60  /* cmd 0xC7 - NEVER called */

/* Higher-level data access (verified V0.27 addresses) */
#define OEM_FLASH_LOAD_SYSCFG       0x0800F918  /* bitmap WL, 0x10000, 30 slots */
#define OEM_FLASH_LOAD_EXTCFG       0x0800FAFC  /* bitmap WL, 0xB000 */
#define OEM_FLASH_CHANNEL_RW        0x0800FA58  /* ch*32 sequential R/W */
#define OEM_FLASH_CHANNEL_WRITE     0x0800EFB0  /* sector R-M-W (128 ch/sector) */
#define OEM_FLASH_BITMAP_SCAN       0x0800EF68  /* find first '1' bit LSB->MSB */

/* These addresses are -0x3000 from prior listing; not yet individually
 * re-verified at corrected offset (marked for future verification): */
#define OEM_FLASH_LOAD_CALIBRATION  0x0800FAFC  /* needs re-check (0xF000 ref) */
#define OEM_FLASH_LOAD_VFOCFG       0x0800FFF8  /* bitmap WL, 0x9000 */
#define OEM_FLASH_LOAD_VFOSEL       0x080100B0  /* bitmap WL, 0xE000 */
#define OEM_FLASH_LOAD_CONTACTS     0x0800FA78  /* marker WL, 0x11000 */
#define OEM_FLASH_WRITE_CHANNEL_WL  0x08010340  /* WL channel config write */
#define OEM_FLASH_SAVE_CHCFG        0x080108F4  /* channel config save */
#define OEM_FLASH_SAVE_VFOCFG       0x08010448  /* VFO config save */
#define OEM_FLASH_SAVE_EXTCFG       0x08010388  /* extended config save */
#define OEM_FLASH_SAVE_SYSCFG       0x080101DC  /* system config save */
#define OEM_FLASH_MASS_ERASE        0x0800F7B4  /* full flash erase */
#define OEM_FLASH_BOOT_INIT         0x080216B8  /* boot-time flash init */

/* ----------------------------------------------------------------------
 * RAM Buffers (used by flash driver, from disassembly literal pools)
 * -------------------------------------------------------------------- */

#define RAM_FLASH_SECTOR_BUF        0x20000C50  /* 4KB temp buffer for R-M-W */
#define RAM_CHANNEL_FREQS           0x2000A394  /* Channel frequency table */
#define RAM_RADIO_SETTINGS          0x2000A7C0  /* Radio config (0x20 bytes) */
#define RAM_RADIO_SETTINGS_EXT      0x2000A7E0  /* Extended config (0x25 bytes) */
#define RAM_RADIO_CONFIG            0x2000A8F9  /* Main config struct (0x99 bytes) */
#define RAM_SYSTEM_CONFIG           0x2000ADD0  /* System config (0x79 bytes) */
#define RAM_CONTACT_NAMES           0x20010130  /* Contact name buffer (0x65 bytes) */
#define RAM_CAL_DATA                0x20000014  /* Calibration data start */

#endif /* SPI_FLASH_H */

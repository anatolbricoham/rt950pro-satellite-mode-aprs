# Protocol and data format — Satellite Mode

*[Versión en español](PROTOCOL_AND_FORMAT.md)*

The normative reference is `include/app/sat_db.h`. `pc/rt950_toolkit/sat.py`
(`HDR_FMT`, `SAT_FMT` and `PASS_FMT`) must match it byte for byte.

For the stock firmware's programming protocol and codeplug map see
[../toolkit/PROTOCOL.en.md](../toolkit/PROTOCOL.en.md).

## 1. SPI flash map (MX25L1606E, 2 MB)

| Range | Size | Use | Written by |
|---|---|---|---|
| 0x000000–0x00FFFF | 64 KB | Channels, VFO, settings, APRS, calibration | OEM CPS / Editor / RT-950 Toolkit / firmware |
| 0x090000–0x0B57FF | 150 KB | 240×320 RGB565 boot image | OEM CPS / Editor / RT-950 Toolkit (custom FW) |
| **0x0C0000–0x0C7FFF** | **32 KB** | **SAT_DB**: header + satellites + passes | `rt950_sat.py upload` / RT-950 Toolkit |
| **0x0C8000–0x0C8FFF** | **4 KB** | **SAT_CFG**: satellite mode settings | firmware (menu) |
| **0x0C9000–0x0C9FFF** | **4 KB** | **APRS_MSG_CFG**: APRS messaging settings | firmware (menu) |
| 0x15C000–0x1FFFFF | — | OEM fonts | factory |

## 2. `satdb.bin`

All little-endian, no padding. CRCs are CRC-32/ISO-HDLC (`zlib.crc32`).

### Header (64 bytes)

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | u32 | magic | `0x44544153` ("SATD") |
| 4 | u16 | version | 1 |
| 6 | u16 | header_size | 64 |
| 8 | u16 | sat_count | ≤ 48 |
| 10 | u16 | sat_rec_size | 128 |
| 12 | u16 | pass_count | ≤ 1024 |
| 14 | u16 | pass_rec_size | 16 |
| 16 | u32 | created_unix | creation UTC |
| 20 | i32 | qth_lat_e6 | degrees × 10⁶ |
| 24 | i32 | qth_lon_e6 | east positive |
| 28 | i16 | qth_alt_m | |
| 30 | i8 | min_el_deg | mask used for the pass table |
| 31 | u8 | flags | bit 0 = QTH present |
| 32 | u32 | pass_start_unix | pass table window |
| 36 | u32 | pass_end_unix | |
| 40 | char[8] | locator | e.g. `IM98ib` |
| 48 | u32 | payload_crc32 | CRC of satellites + passes |
| 52 | u8[8] | reserved | 0 |
| 60 | u32 | header_crc32 | CRC of bytes 0..59 |

### Satellite (128 bytes)

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | char[12] | name | shown on the radio, NUL padded |
| 12 | u32 | norad | |
| 16 | f64 | epoch_jd | TLE epoch as UTC Julian date |
| 24 | f64 | bstar | |
| 32 | f64 | incl_deg | |
| 40 | f64 | raan_deg | |
| 48 | f64 | ecc | |
| 56 | f64 | argp_deg | |
| 64 | f64 | mean_anom_deg | |
| 72 | f64 | mean_motion_rpd | revolutions per day (Kozai, as in the TLE) |
| 80 | u32 | downlink_hz | |
| 84 | u32 | uplink_hz | 0 = RX only |
| 88 | u16 | ctcss_up_dhz | tenths of Hz; 0 = no tone |
| 90 | u16 | ctcss_down_dhz | |
| 92 | u16 | arm_tone_dhz | SO-50: 744 |
| 94 | u8 | mode | 0 FM, 1 FM_DATA, 2 LIN_INV, 3 LIN, 4 RX_ONLY |
| 95 | u8 | flags | 1 enabled, 2 favorite, 4 deep-space, 8 scheduled, 16 sunlit-only |
| 96 | u32 | passband_hz | linear satellites |
| 100 | char[24] | info | free text |
| 124 | u8[4] | reserved | |

### Pass (16 bytes), sorted by AOS

| Off | Type | Field |
|---|---|---|
| 0 | u32 | aos_unix |
| 4 | u16 | duration_s |
| 6 | u16 | tca_offset_s |
| 8 | u8 | sat_index |
| 9 | u8 | max_el_deg |
| 10 | u16 | aos_az_deg |
| 12 | u16 | los_az_deg |
| 14 | u16 | tca_az_deg |

## 3. Custom firmware CPS protocol

Serial port at 115200 8N1 on the Kenwood connector (UART4).

### Frame

```
A5 FF FF FF <cmd> <len> <payload[len]> <crcH> <crcL>
```

The CRC is CRC-16/CCITT (polynomial 0x1021, initial 0x0000) over bytes
0..5+len.

### Session

1. The PC sends `PROGRAMBT9000U`. After 5 bytes the radio enters programming
   mode and answers with `R` + `"RT-950      "`.
2. The PC sends `I`. If there is no answer it aborts without writing.
3. For each 4 KB sector: `E` + 3-byte address (big-endian); the radio
   answers `E`.
4. For each 128-byte block: `W` + 3-byte address + data; the radio answers
   `W`.
5. Verification: `R` + 3-byte address + length; the radio answers `R` +
   data.
6. `K` + u32 Unix time, little-endian (sets UTC); the radio answers `K`.
7. `L` (reload the database); the radio answers `L` + number of satellites
   (0xFF = error).
8. `X` (end of write): the radio returns to normal operation.

### Added commands

| Cmd | Request | Answer |
|---|---|---|
| `I` 0x49 | — | `"SAT"`, API version, DB address (3 bytes, BE), size in KB, satellites loaded, time source |
| `K` 0x4B | u32 Unix (LE) | empty frame. Ignored if earlier than November 2023 |
| `L` 0x4C | — | 1 byte: number of satellites or 0xFF |

While the CPS session is active, the satellite engine does not read the
flash.

## 4. `sat_cfg_t` configuration (36 bytes, at 0x0C8000)

`magic` "SCFG", `version`, `selected`, `min_el_deg`, `doppler_on`, `tx_mode`,
`loc_src`, `alert_min`, `rx_wide`, `manual_lat_e6`, `manual_lon_e6`,
`manual_alt_m`, `manual_valid`, reserved and CRC-32 of bytes 0..31. If the
CRC does not match, defaults are loaded: Doppler on, TX on the same chip,
Auto, alert at 2 minutes and wide RX.

## 5. Integrating it into another program

RT-950 Toolkit already does it (Satellites tab). To add it to another
program, for example RT-950/950Pro Editor, a **Tools > Satellites** option
doing what `rt950_sat.py` does would be enough:

1. Download TLEs.
2. Build the `satdb.bin` of section 2.
3. If the radio answers `I`, follow the session of section 3.
4. If it does not answer (OEM firmware), offer to import the Doppler
   channels, which is already possible with the current CSV importer.

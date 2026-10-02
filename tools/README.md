# RT-950 Pro Tools

## encrypt_btf.py

Encrypts raw firmware binaries into the Radtel `.BTF` update format, or decrypts
`.BTF` files back to raw binary for analysis.

### Usage

**Encrypt** a raw binary to BTF format:
```bash
python3 encrypt_btf.py firmware.bin firmware.BTF --key DC24DEF7CF4F3FED91BCC88BB0613A51
```

**Decrypt** a BTF file to raw binary:
```bash
# Auto-extract key from BTF header (offset 0x400)
python3 encrypt_btf.py --decrypt firmware.BTF firmware.bin --auto

# Manual key
python3 encrypt_btf.py --decrypt firmware.BTF firmware.bin --key DC24DEF7CF4F3FED91BCC88BB0613A51
```

Options:
- `--key <hex>` - 32-character hex encryption key
- `--decrypt` - Decrypt mode (BTF to bin)
- `--auto` - Extract key from BTF header automatically (decrypt only)
- `--verbose` - Print operation details and vector table validation

The tool recognizes all known firmware version keys and prints the version when matched.

## Encryption Scheme

1. The first 0x800 (2048) bytes are **not encrypted** - they contain the vector table and bootloader signature block.
2. From offset 0x800 onward, bytes are XORed with a 128-byte expanded key.
3. Bytes with value `0x00` or `0xFF` are **never encrypted** (preserved as-is).
4. The 128-byte key is generated from a 16-byte seed by iteratively bit-rotating the left and right halves (see key expansion below).

## Known Keys

| Version | Key |
|---------|-----|
| V0.15 | `71CAEFACD047EF83EFD2141A3512A638` |
| V0.18 | `DC24DEF7CF4F3FED91BCC88BB0613A51` |
| V0.21 | `3C0F640BB03230BB97AF8029C4AD794D` |
| V0.27 | `7E807B1761A4EBC6FC3A8DD33752F305` |

Keys are stored at offset **0x400** (16 bytes) in the `.BTF` file.

## Key Expansion

The 16-byte seed is expanded to 128 bytes (8 x 16-byte blocks):

- **Block 0**: original key
- **Blocks 1-7**: per-byte bit rotation from previous block
  - Bytes 0-7: left-rotate by 1 bit (`(b << 1) | (b >> 7)`)
  - Bytes 8-15: right-rotate by 1 bit (`(b >> 1) | (b << 7)`)

---

## cps_flash.py

Reads and writes the radio's configuration memory over the CPS serial
programming protocol. Reverse engineered from the OEM BT-RT950PRO_CPS.exe
(.NET assembly).

Requires: `pyserial` (`pip install pyserial`)

### Usage

```bash
# Show radio model, identity, and connection info
python3 cps_flash.py info /dev/ttyUSB0

# Read 64 KB of flash starting at address 0
python3 cps_flash.py read /dev/ttyUSB0 dump.bin --addr 0x0000 --len 0x10000

# Write a config file to flash (default addr 0x8000, above channel area)
python3 cps_flash.py write /dev/ttyUSB0 config.bin --addr 0x8000

# Write with verification readback
python3 cps_flash.py write /dev/ttyUSB0 config.bin --addr 0x8000 --verify

# Dump all known CPS regions to individual files
python3 cps_flash.py dump /dev/ttyUSB0 flash_dump/

# Dump including calibration probe addresses
python3 cps_flash.py dump /dev/ttyUSB0 flash_dump/ --probe

# Enable verbose protocol debug output
python3 cps_flash.py -v info /dev/ttyUSB0
```

### CPS Protocol Summary

| Parameter | Value |
|-----------|-------|
| Interface | UART4 (PC10 TX, PC11 RX) via Kenwood-style 3.5mm jack |
| Baud rate | 115,200 8N1, no flow control |
| Handshake | "PROGRAMBT9000U" (14 bytes) + ACK + identity + model + key negotiation |
| Encryption | 4-byte XOR key from 20-entry table, negotiated per session |
| Read cmd | `{0x52, addr_hi, addr_lo, length}` - raw bytes, no framing |
| Write cmd | `{0x57, addr_hi, addr_lo, length, data...}` - raw bytes, no framing |
| Block size | 128 bytes max per transfer |
| Session end | Single byte 0x06 (read) or 0x45 (write) |

### Address Regions

| Address | Size | Contents |
|---------|------|----------|
| 0x0000-0x7BBF | 31,680 | Channel data (990 x 32 bytes) |
| 0x8000-0x807F | 128 | VFO config (3 VFOs x 32 + reserved) |
| 0x9000-0x907F | 128 | System config (3 parts) |
| 0xA000-0xA17F | 384 | Extended config 1 |
| 0xB000-0xB0FF | 256 | Extended config 2 |
| 0xC000-0xC0FF | 256 | DTMF / modulation |
| 0xD000-0xD2FF | 768 | APRS / misc |
| 0xF000-0xF07F | 128 | Calibration data (probe) |
| 0xF200-0xF27F | 128 | Calibration data (probe) |

---

## firmware_upload.py

Uploads BTF firmware files to the radio using the bootloader update protocol.
Reverse engineered from `RT-950_EnUPDATE.exe` (.NET assembly) and the OEM
bootloader disassembly.

Requires: `pyserial` (`pip install pyserial`)

### Usage

```bash
# Upload firmware (normal mode - sends PROGRAMBT9000U + UPDATE handshake first)
python3 firmware_upload.py upload /dev/ttyUSB0 RT_950Pro_V0.27.BTF

# Upload firmware (PTT mode - radio already in bootloader via PTT held at power-on)
python3 firmware_upload.py upload /dev/ttyUSB0 RT_950Pro_V0.27.BTF --ptt

# Validate a BTF file without uploading (checks model string, vector table, key)
python3 firmware_upload.py verify RT_950Pro_V0.27.BTF

# Probe if radio is in bootloader mode
python3 firmware_upload.py probe /dev/ttyUSB0

# Verbose output (show raw TX/RX bytes)
python3 firmware_upload.py upload /dev/ttyUSB0 firmware.BTF -v
```

### Bootloader Protocol Summary

| Parameter | Value |
|-----------|-------|
| Interface | Same UART4 as CPS (Kenwood-style 3.5mm jack) |
| Baud rate | 115,200 8N1, no flow control |
| Phase 1 | "PROGRAMBT9000U" + ACK + "UPDATE" + ACK (enters bootloader) |
| Phase 2 | 0xAA-framed packets with CRC-CCITT |
| Packet format | `[0xAA, cmd, args_hi, args_lo, len_hi, len_lo, data..., crc_hi, crc_lo, 0x55]` |
| CRC | CRC-CCITT poly 0x1021, init 0x0000 over cmd through data |
| Response | `[0xAA, cmd, 0x00, result, 0x00, 0x00, crc_hi, crc_lo, 0x55]` |
| Data block size | 1024 bytes per packet |

### Command Sequence

| Step | Cmd | Data | Description |
|------|-----|------|-------------|
| 1 | 0x42 | (none) | Probe bootloader (returns 0xE5 error, expected) |
| 2 | 0x0A | "BOOTLOADER_V3" | Version handshake |
| 3 | 0x02 | 32 bytes from BTF@0x3E0 | Model signature verification |
| 4 | 0x04 | 2 bytes: total_pkgs-1 (BE) | Announce package count |
| 5 | 0x03 | 1024 bytes (repeated) | Firmware data transfer |
| 6 | 0x45 | (none) | Finalize update |

### Response Codes

| Code | Meaning |
|------|---------|
| 0x06 | ACK / success |
| 0xE1 | Wrong data length |
| 0xE2 | Data verification error |
| 0xE3 | Flash write error |
| 0xE5 | Unknown command |
| 0xE6 | Model mismatch |

### Notes

- The BTF file is sent as-is (still encrypted). The bootloader decrypts internally.
- The bootloader has NO read/download commands. Firmware readback is impossible via UART.
- To force bootloader mode: hold keys 3 and 4 (side bottom two buttons) while powering on the radio.
- See `tools/firmware_upload.py` for the upload protocol implementation.

---

## rt950_sat.py

Satellite support (both stock and custom firmware). Requires `sgp4`, `pyserial`.
Thin wrapper around `pc/rt950_toolkit/sat.py`, which RT-950 Toolkit also uses.

```bash
python3 rt950_sat.py all --locator IM98IB              # report + CSV + satdb.bin
python3 rt950_sat.py all --locator IM98IB --port COM5  # + upload (custom FW)
python3 rt950_sat.py chirp --start-channel 900         # stock FW Doppler channels
python3 rt950_sat.py upload --port /dev/ttyUSB0        # upload rt950_sat_out/satdb.bin
python3 rt950_sat.py settime --port /dev/ttyUSB0       # set radio UTC clock
python3 ../pc/run_toolkit.py                           # GUI (RT-950 Toolkit)
```

Outputs (`rt950_sat_out/`): `pases_satelites.html|csv|ics`,
`canales_satelite_chirp.csv` (open it in RT-950/950Pro Editor or CHIRP),
`satdb.bin`, `tle_cache.txt`. Frequencies live in `pc/rt950_toolkit/data/sat_freqs.json`.
Format and protocol: `docs/satellite/PROTOCOL_AND_FORMAT.en.md`.


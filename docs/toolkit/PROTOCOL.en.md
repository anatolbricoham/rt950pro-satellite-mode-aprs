# Programming protocol and codeplug map — RT-950 / RT-950 Pro

*[Versión en español](PROTOCOL.md)*

What `pc/rt950_toolkit/protocol.py` and `codeplug.py` implement. Sources are
the maker's model file (`pc/rt950_toolkit/data/rt950pro_schema.json`), the
open firmware's reverse-engineering notes (`include/drivers/flash_layout.h`)
and RT-950/950Pro Editor (KK4OXN, MIT), which was used to cross-check every
detail (see D-30).

## 1. Stock Radtel firmware

Serial port at 115200 8N1.

### Session

| Step | PC → radio | Radio → PC |
|---|---|---|
| 1 | `PROGRAMBT9000U` (14 ASCII bytes) | `06` |
| 2 | `F` | 16-byte identity |
| 3 | `M` | 12-byte model, e.g. `RT-950 Pro` + padding |
| 4 | `SEND` + 21 bytes (challenge) | `06` |
| 5 | read or write blocks (below) | |
| 6 | `45` (`E`) after writing, `06` after reading only | — |

The program checks that the model contains `RT-950` before going on.

### Challenge and key

The step 4 challenge selects one of 20 four-byte keys. The maker's CPS and
the Editor always send the same challenge:

```
53 45 4E 44 11 10 0F 06 03 01 13 02 13 0E 06 0C 0D 0C 12 04 11 0D 0B 0E 00
```

Byte 4 (`sel`) gives the index position:
`idx = ((sel − 0x20)·2 + 1 if sel & 0x20, else (sel − 0x10)·2) + 1`, and the
key is `TABLE[challenge[4 + idx]]`. This challenge yields the key `"RVB "`.

### Data scrambling

Each data byte `b`, with `k = key[i mod 4]`, is sent as `b ^ k` **except**
when `k == 0x20`, `b == 0x00`, `b == 0xFF`, `b == k` or `b == k ^ 0xFF`; then
it is sent unchanged. The same function scrambles and unscrambles.

### Blocks

```
read:  R  aH aL n            → R aH aL n + n scrambled bytes
write: W  aH aL n + data      → 06
```

`n` is 128 (or the remainder at the end of a region). Addresses are 16-bit.

The model file addresses the APRS page as 0xFFFF…, but it is transferred
with its own commands at address 0:

```
APRS read:  T 00 00 80          → T 00 00 80 + 128 bytes
APRS write: X 00 00 80 + data    → 06   (the radio is slow to confirm: up to 30 s)
```

### Transfer plan

| Region | Address | Size | Commands |
|---|---|---|---|
| channels | 0x0000 | 0x7C00 | R / W |
| vfo | 0x8000 | 0x80 | R / W |
| settings | 0x9000 | 0x80 | R / W |
| dtmf | 0xA000 | 0x180 | R / W |
| modulation (FM/AM/SSB) | 0xB000 | 0x100 | R / W |
| zones | 0xC000 | 0x100 | R / W |
| mod_names | 0xD000 | 0x300 | R / W |
| aprs | (0xFFFF) | 0x80 | T / X at address 0 |

## 2. Custom firmware

`A5 FF FF FF <cmd> <len> <payload> <crcH> <crcL>` frames with CRC-16/CCITT
and 24-bit addresses into the SPI flash (see
[PROTOCOL_AND_FORMAT.en.md](../satellite/PROTOCOL_AND_FORMAT.en.md)). The
firmware answers `PROGRAMBT9000U` with an `A5` frame instead of `06`, which is
how `protocol.detect()` tells them apart. Regions are written with a
read-modify-write of each 4 KB sector so neighbouring data is not erased.

## 3. Encodings

### Channel (32 bytes, channel `i` at `i × 32`)

| Bytes | Field | Encoding |
|---|---|---|
| 0–3 | RX frequency | packed BCD, least significant byte first, 10 Hz units. `FF FF FF FF` = empty channel |
| 4–7 | TX frequency | same |
| 8–9 | RX tone | see below |
| 10–11 | TX tone | see below |
| 12 | Signal code | 0–15 (shown as 1–16) |
| 13 | PTT-ID | 0 OFF, 1 BOT, 2 EOT, 3 BOTH |
| 14 | Power / scrambler | bits 0–3: 0 high, 1 mid, 2 low; bits 4–7: scrambler 0 (OFF) to 8 |
| 15 | Options | bit 0 AM, bit 1 TX enable, bit 2 scan add, bit 3 busy lock, bits 4–5 encryption (0–3), bit 6 narrow, bit 7 FHSS learn |
| 16–19 | FHSS code | preserved as is |
| 20–31 | Name | GBK (code page 936), `FF` padded |

### Tones (2 bytes)

| Value | Meaning |
|---|---|
| `00 00` or `FF FF` | no tone |
| `n 00`, n = 1–105 | DCS normal: code n of the DCS list (023, 025, …) |
| `n 00`, n = 106–210 | DCS inverted: code n − 105 |
| other | CTCSS: frequency × 10, little-endian (67.0 Hz → `9E 02`) |

### Other fields

| Field | Encoding |
|---|---|
| Zone names | 10 × 16 bytes at 0xC000, GBK, `FF` padded |
| VFO frequency | one digit per byte: `1 4 5 5 2 5 0 0` = 145.52500 MHz (A at 0x8000, B at 0x8020, C at 0x8040) |
| VFO offset | 7 digits at +0x14 |
| FM memories | u16 little-endian, MHz × 100 |
| AM and SSB memories | u16 little-endian, kHz. 5-byte SSB row: frequency (2), bandwidth (1), signed BFO offset (2) |
| DTMF codes | one digit per byte (0–9, A–D = 10–13, `*` = 14, `#` = 15), `FF` padded |
| APRS call sign | 6 upper-case bytes at APRS+0x11, SSID at APRS+0x17 |
| Select settings | byte + bit positions from the model file |

## 4. CPS `.dat` file

It is a .NET `KDH.RadioData` object serialized with BinaryFormatter
(MS-NRBF). `nrbf.py` reads it and writes it back byte-for-byte identical;
`datfile.py` maps its lists (channels, zone names, function table, APRS bytes
and text) to the codeplug and back. Saving starts from an existing `.dat` and
only changes values, so fields the program does not know are preserved.

# CPS UART Protocol (UART4)

Programming protocol between the RT-950 Pro and its CPS software (BT-RT950PRO_CPS.exe).
Reverse-engineered from CPS v1.3.0 (.NET) and V0.27 firmware UART4 ISR at `0x08024E95`.

## Physical Layer

| Parameter | Value |
|-----------|-------|
| Interface | UART4 (PC10 TX, PC11 RX) |
| Baud rate | 115200 |
| Format | 8N1, no flow control |

## Frame Format

```
Offset  Field           Size    Description
0       Header          1       0xA5
1-3     Padding         3       0xFF 0xFF 0xFF
4       Command         1       See Command Table
5       Length          1       Payload byte count
6..5+N  Payload         N       Command-specific
6+N     CRC-16 High     1       CRC-CCITT MSB
7+N     CRC-16 Low      1       CRC-CCITT LSB
```

Minimum frame: 8 bytes (no payload).

## CRC Algorithm

CRC-CCITT: polynomial `0x1021`, initial `0x0000`, no final XOR.
Covers bytes 0 through 5+N (everything except CRC bytes).

```c
uint16_t crc_ccitt(const uint8_t *data, int len) {
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
```

## Commands

| Byte | ASCII | Name | Direction | Description |
|------|-------|------|-----------|-------------|
| 0x52 | `R` | Read | CPS -> Radio | Request data read |
| 0x57 | `W` | Write | CPS -> Radio | Write data to radio |
| 0x54 | `T` | ReadEnd | CPS -> Radio | Read complete, exit |
| 0x58 | `X` | WriteEnd | CPS -> Radio | Write complete, exit |
| 0x45 | `E` | Erase | CPS -> Radio | Erase flash block |

## Handshake

1. Radio enters handshake state (state machine at RAM `0x2000B240`)
2. CPS sends 5 bytes one at a time over UART4
3. Firmware counts bytes; at count=5:
   - Mutes BK4829 (REG_51 = 0x0300) via `0x0801EC08`
   - Sets programming flag
   - Transitions to data-transfer state

Handshake frame:
```
TX: [0xA5][0xFF][0xFF][0xFF][Cmd][0x00][CRC_H][CRC_L]
```

## Model Validation

After handshake, firmware authenticates:
1. Reads MCU UID from `0x1FFFF7E8` (12 bytes)
2. Reads bootloader model data from `0x08002E00` (12 bytes)
3. XORs the two -> authentication token
4. Compares against `"RT-950      "` (16 bytes, space-padded)
5. On mismatch -> `MODELERR`

## Data Transfer

### Read (CPS reads from radio)

```
CPS -> Radio:  Read cmd [R][AddrH][AddrL][Len]
Radio -> CPS:  [A5][FF][FF][FF][data...][CRC_H][CRC_L]
... repeat per block ...
CPS -> Radio:  ReadEnd [T]
```

### Write (CPS writes to radio)

```
CPS -> Radio:  Erase cmd [E][AddrH][AddrL]
Radio -> CPS:  ACK
CPS -> Radio:  Write cmd [W][AddrH][AddrL][data...][CRC]
Radio -> CPS:  ACK
... repeat per block ...
CPS -> Radio:  WriteEnd [X]
```

Packet size: 128 bytes. Erase block: 256 bytes.

## Flash Address Map (CPS View)

| Address | Size | Content |
|---------|------|---------|
| 0x00000-0x07BC0 | 31,680 | Channel data (990 x 32 bytes) |
| 0x08000-0x08080 | 128 | VFO config (3 VFOs x 32 bytes) |
| 0x09000-0x09080 | 128 | System settings |
| 0x0A000-0x0A180 | 384 | Extended config part 1 |
| 0x0B000-0x0B100 | 256 | Extended config part 2 |
| 0x0C000-0x0C100 | 256 | DTMF + modulation config |
| 0x0D000-0x0D300 | 768 | APRS + misc settings |

## Image Transfer (Splash Screen)

Uses `ImportBmpOperation` flow:
1. Handshake (same as data)
2. Set target address: `0x090000` in SPI flash
3. Erase image sectors
4. Write 240x320 RGB565 data (153,600 bytes)
5. Complete

Input: 24-bit BMP -> converted to RGB565 raw pixels.

## Firmware ISR State Machine

UART4 ISR vector `0x08003110` -> handler `0x08024E95`:

| State | Description |
|-------|-------------|
| 0 | Idle - not in CPS mode |
| 1 | Handshake - counting bytes, transition at count=5 |
| 2 | Data transfer - dispatches R/W/E commands via SPI flash |
| 3 | Done - returns to normal operation |

State variable at RAM `0x2000B240`:
- `[0]`: State (0-3)
- `[4]`: Operation type (1=read, 2=write)
- `[8]`: Address/size
- `[0xC]`: Transfer active flag

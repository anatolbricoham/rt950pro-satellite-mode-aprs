# Bluetooth (BLE) Protocol

Reverse-engineered from the "walkie-talkie tool" Android app v1.3.1.
Package: `com.uniapp.kdh.uniplugin_kdh` (UniApp hybrid with native BLE plugin).

## Physical Layer

BLE (Bluetooth Low Energy) via GATT - **not** Classic Bluetooth SPP.

### GATT Characteristics

| UUID Fragment | Role | Description |
|---------------|------|-------------|
| `ff31` | Control | Authentication, AT commands to BLE module |
| `ffe1` | Data (fallback) | R/W data channel when ff32 absent |
| `ff32` | Data (preferred) | Primary data channel (enabled first if present) |

On connection: MTU negotiated to 512. If `ff32` exists, notifications enabled on
`ff32` and disabled on `ffe1`; otherwise `ffe1` used as data channel.

### BLE Module AT Commands

Sent via `ff31` characteristic:

| Command | Description |
|---------|-------------|
| `AT+BAUD=N` | Set baud rate (N=0-9) |
| `AT+RST` | Reset BLE module |
| `AT+BAUD?` | Query baud rate |

Baud rate mapping: 5=9600, 6=115200, 7=57600, default=query.

## Authentication

### Step 1: Serial Number (App to Radio)

20-byte serial via `ff31`:
- Bytes 0-3: `0x3F`
- Byte 4: Baud rate selector
- Bytes 5-19: Random values 0-98

### Step 2: Secret Key Challenge (Radio to App)

Radio responds with 20-byte challenge. App computes expected key:

**Algorithm 1 (original):**
```
key[0..4] = 0x21
key[i] = sum(serial[0..i]) % (serial[19-(i-5)] + (i-4))   for i >= 5
```

**Algorithm 2 (new):**
```
key[0..4] = 0x21
key[i] = sum(serial[24-i-j] for j=0..5) % (serial[24-i] + (25-i))   for i >= 5
```

App accepts either algorithm match.

### Step 3: Model-Specific Handshake

Fetched from KDH server per-model (see `kdh-server.md`):
```
POST brandModel/findIdentSettingByModel { modelId: <id> }
```
Returns `handCode` array of [TX, RX, TX, RX, ...] pairs.

## Data Transfer

### CRC

Same CRC-CCITT as CPS protocol: polynomial `0x1021`, initial `0x0000`.

### Packet Framing

When `radioAgreement.getCrc() > 0`:
```
[CRC_ID][Len][Payload...][CRC_H][CRC_L]
```

### Read/Write Commands

| Command | Format | Description |
|---------|--------|-------------|
| Read (RT-950) | `[0x52][AddrH][AddrL][0x08]` | Read 8 bytes per packet |
| Write (RT-950) | Same frame as CPS write | Standard write protocol |
| ReadEnd | `[0x54]` | Read complete |
| WriteEnd | `[0x58]` | Write complete |

### Data Encryption (Optional)

When `radioAgreement.getEncry() > 0`:
- 4-byte XOR key from 20-entry lookup table (`encryIndexBuf`)
- Key index from handshake step 2 (type=2 packet)
- Cyclic XOR, skipping: 0x00, 0xFF, key byte, 0x20 (space)

### BLE Chunking

- Default: 20 bytes (BLE minimum)
- With MTU: up to 133 bytes (firmware update)
- `delayTime` ms between chunks

## Firmware Update (BLE Module MCU)

GT12-style protocol over BLE for updating the BLE module itself (not the AT32F403A):

1. Auth via `ff31`, verify secret key
2. Switch to `ffe1`, send `PROGRAMGT12` identifier
3. Read version, read config
4. Erase from `0x001C0000`, block count = ceil(fileLen/64KB)
5. Write 128-byte chunks, 8 per 1KB block with CRC verify
6. Full-file CRC verification
7. Send completion command (0x45)

**Note:** Write addresses (`0x001C0000+`) target the BLE module flash, not the radio MCU.

### Update Frame Format

```
[0x01][Len][Cmd][Params...][CRC_H][CRC_L]
```

| Cmd | Description |
|-----|-------------|
| 0x05 | Write 128-byte data chunk |
| 0x06 | CRC check of 1KB block |
| 0x10 | Identify ("PROGRAMGT12") |
| 0x12 | Set write address |
| 0x13 | Erase flash sectors |
| 0x14 | Full-file CRC verify |
| 0x45 | End update |

## CPS vs BLE Comparison

| Feature | CPS (UART4) | BLE (App) |
|---------|-------------|-----------|
| Transport | UART 115200 | BLE GATT |
| Frame header | 0xA5 | Per-model config |
| CRC | CRC-CCITT | CRC-CCITT (same) |
| Auth | MCU UID XOR | 20-byte serial + key derivation |
| Encryption | None | Optional 4-byte XOR |
| Packet size | 128 bytes | 8 bytes (configurable) |

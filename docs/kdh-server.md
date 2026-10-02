# KDH Server API

The RT-950 Pro's Android app and CPS fetch per-model configuration from KDH's
cloud server. This drives handshake codes, protocol parameters, and firmware
update addresses - making the protocol partially server-defined at runtime.

## Endpoints

| URL | Purpose |
|-----|---------|
| `https://www.kediheng.com/dcAdmin/` | REST API base |
| `ws://www.kediheng.com:9326` | WebSocket (real-time) |

All requests: `POST`, JSON body with `{"data":{...,"areaToken":"kdh","language":"en"}}`.

## API Routes

| Endpoint | Purpose |
|----------|---------|
| `brand/brandList` | List all radio brands |
| `brandModel/modelList` | List models for a brand |
| `brandModel/findIdentSettingByModel` | Handshake/identification config |
| `brandModel/findSettingByModel` | **Full radio protocol configuration** |
| `brandModel/findReadSettingByModel` | Read-specific overrides (null for RT-950) |
| `brandModel/findWriteSettingByModel` | Write-specific overrides (null for RT-950) |
| `brandModel/findUsbSettingByModel` | USB config (null for RT-950) |
| `brandModel/findMCUByModel` | BLE module firmware binary |
| `brandModel/pediaRecognizeMuch` | Multi-model auto-detect |

## RT-950 PRO Identifiers

| Field | Value |
|-------|-------|
| Brand | Radtel (ID: `86180044171640832`) |
| Model | RT-950 PRO (ID: `93334402147549184`) |
| Config name | `RT-950PRO Series` |
| Handshake ID | `PROGRAMBT9000U` |
| Baud rate code | 7 (57600) |

## Protocol Agreement (from `findSettingByModel`)

```
type:              5
encry:             1 (encryption enabled)
crc:               0 (CRC in framing = no; CRC-CCITT used separately)
start:             8 (data offset in frame)
delayTime:         5 ms (inter-chunk BLE delay)
readPackDataNum:   128 bytes/packet
writePackDataNum:  128 bytes/packet
lockPassword:      [0, 0, 0, 0]
```

## Handshake Sequence (from `findIdentSettingByModel`)

| Step | Type | Dir | Data | Description |
|------|------|-----|------|-------------|
| 0 | 1 (initiate) | TX | `PROGRAMBT9000U` | Enter programming mode |
| 1 | 2 (cmd) | TX | `F` (0x46) | Request firmware info |
| 2 | 3 (verify) | TX | `M` (0x4D) | Request model string |
| 3 | 6 (verify+len) | TX | `SEND` | Signal ready |
| 4-5 | 0 (NOP) | - | - | Padding |
| 6 | 7 (read tmpl) | - | `R,0,0,0x80,T,0,0,0x80` | Read: cmd=R, end=T, 128B |
| 7 | 8 (write tmpl) | - | `W,0,0,0x80,X,0,0,0x80` | Write: cmd=W, end=X, 128B |
| 8 | 0 (ACK) | RX | 0x06 | ACK byte |

## Flash Address Map (Server-Defined)

From the `addr[]` array in server config:

| Index | Address | Region |
|-------|---------|--------|
| 0 | 0x000000 | Channel data start |
| 1 | 0x007BC0 | Channel data end (990 x 32 = 31,680 bytes) |
| 2 | 0x008000 | VFO A config |
| 3 | 0x008080 | VFO config end |
| 4 | 0x009000 | System settings |
| 5 | 0x009080 | System settings end |
| 6 | 0x00A000 | Extended config start |
| 7 | 0x00D300 | Extended config end |
| 8 | 0x00FFFF | APRS/GPS config |
| 9 | 0x01007F | APRS/GPS config end |
| 10 | 0xFFFFFF | Sentinel |

## Data Structures (Server-Side Definitions)

These field definitions come from the server config and match CPS behavior.
See `cps-uart.md` for the wire protocol. Key structures:

### Channel Record (32 bytes)

| Byte(s) | Field | Format |
|---------|-------|--------|
| 0-3 | RX Frequency | BCD |
| 4-7 | TX Frequency | BCD |
| 8-11 | RX/TX CTCSS | Tone index (high+low bytes) |
| 12 | Signal Code | 0-14 |
| 13 | PTT-ID | 0=OFF, 1=BOT, 2=EOT, 3=BOTH |
| 14[3:0] | Power | 0=High, 1=Mid, 2=Low |
| 14[7:4] | Scramble | 0=OFF, 1=ON |
| 15 | Flags | Bitfield: mod, tx_en, scan, bcl, encr, bw |
| 16-27 | Name | 12 bytes ASCII |
| 28-31 | Reserved | |

### System Settings (0x9000)

| Offset | Field | Values |
|--------|-------|--------|
| +0 | Squelch | 0-9 |
| +1 | Power Save | OFF/Std/Enhanced/Extreme |
| +2 | VOX Level | 1-9 |
| +3 | Auto Backlight | AlwaysOn / 5s-3min |
| +4 | Dual Watch | OFF/ON |
| +5 | TOT | OFF / 30s-240s |
| +6 | Beep | OFF/ON |
| +7 | Voice Prompt | OFF/ON |
| +8 | Language | English/Chinese |
| +13-15 | Display Mode A/B/C | Name/Freq/Num |

### Side Key Functions

| Index | Function |
|-------|----------|
| 0 | Radio |
| 1 | Monitor |
| 2 | Scan |
| 3 | Sweep Freq |
| 4 | Alarm |
| 5 | Frequency Spectrum |
| 6 | Beacon Transmission |
| 7 | PTTC |

## Notes
- Full 76KB server response cached in `binary/rt950_server_config.json`
- Server config is authoritative for protocol parameters - the app uses these
  values at runtime, not hardcoded constants
- Multiple radio brands/models share this same server infrastructure

# Protocolo y formato de datos — Modo Satélite

*[English version](PROTOCOL_AND_FORMAT.en.md)*

La referencia normativa es `include/app/sat_db.h`. `pc/rt950_toolkit/sat.py`
(`HDR_FMT`, `SAT_FMT` y `PASS_FMT`) debe coincidir byte a byte con ella.

El protocolo de programación del firmware original y el mapa del codeplug
están en [../toolkit/PROTOCOL.md](../toolkit/PROTOCOL.md).

## 1. Mapa de la flash SPI (MX25L1606E, 2 MB)

| Rango | Tamaño | Uso | Quién lo escribe |
|---|---|---|---|
| 0x000000–0x00FFFF | 64 KB | Canales, VFO, ajustes, APRS, calibración | CPS OEM / Editor / RT-950 Toolkit / firmware |
| 0x090000–0x0B57FF | 150 KB | Imagen de arranque 240×320 RGB565 | CPS OEM / Editor / RT-950 Toolkit (FW custom) |
| **0x0C0000–0x0C7FFF** | **32 KB** | **SAT_DB**: cabecera + satélites + pases | `rt950_sat.py upload` / RT-950 Toolkit |
| **0x0C8000–0x0C8FFF** | **4 KB** | **SAT_CFG**: ajustes del modo satélite | firmware (menú) |
| **0x0C9000–0x0C9FFF** | **4 KB** | **APRS_MSG_CFG**: ajustes de la mensajería APRS | firmware (menú) |
| 0x15C000–0x1FFFFF | — | Fuentes OEM | fábrica |

## 2. `satdb.bin`

Todo en little-endian y sin relleno. Las CRC son CRC-32/ISO-HDLC
(`zlib.crc32`).

### Cabecera (64 bytes)

| Off | Tipo | Campo | Notas |
|---|---|---|---|
| 0 | u32 | magic | `0x44544153` ("SATD") |
| 4 | u16 | version | 1 |
| 6 | u16 | header_size | 64 |
| 8 | u16 | sat_count | ≤ 48 |
| 10 | u16 | sat_rec_size | 128 |
| 12 | u16 | pass_count | ≤ 1024 |
| 14 | u16 | pass_rec_size | 16 |
| 16 | u32 | created_unix | UTC de creación |
| 20 | i32 | qth_lat_e6 | grados × 10⁶ |
| 24 | i32 | qth_lon_e6 | este positivo |
| 28 | i16 | qth_alt_m | |
| 30 | i8 | min_el_deg | máscara usada para la tabla de pases |
| 31 | u8 | flags | bit 0 = hay QTH |
| 32 | u32 | pass_start_unix | ventana de la tabla |
| 36 | u32 | pass_end_unix | |
| 40 | char[8] | locator | p. ej. `IM98ib` |
| 48 | u32 | payload_crc32 | CRC de satélites + pases |
| 52 | u8[8] | reserved | 0 |
| 60 | u32 | header_crc32 | CRC de los bytes 0..59 |

### Satélite (128 bytes)

| Off | Tipo | Campo | Notas |
|---|---|---|---|
| 0 | char[12] | name | se muestra en la radio, relleno con NUL |
| 12 | u32 | norad | |
| 16 | f64 | epoch_jd | época del TLE como fecha juliana UTC |
| 24 | f64 | bstar | |
| 32 | f64 | incl_deg | |
| 40 | f64 | raan_deg | |
| 48 | f64 | ecc | |
| 56 | f64 | argp_deg | |
| 64 | f64 | mean_anom_deg | |
| 72 | f64 | mean_motion_rpd | revoluciones por día (Kozai, como el TLE) |
| 80 | u32 | downlink_hz | |
| 84 | u32 | uplink_hz | 0 = solo RX |
| 88 | u16 | ctcss_up_dhz | décimas de Hz; 0 = sin tono |
| 90 | u16 | ctcss_down_dhz | |
| 92 | u16 | arm_tone_dhz | SO-50: 744 |
| 94 | u8 | mode | 0 FM, 1 FM_DATA, 2 LIN_INV, 3 LIN, 4 RX_ONLY |
| 95 | u8 | flags | 1 enabled, 2 favorite, 4 deep-space, 8 scheduled, 16 sunlit-only |
| 96 | u32 | passband_hz | lineales |
| 100 | char[24] | info | texto libre |
| 124 | u8[4] | reserved | |

### Pase (16 bytes), ordenado por AOS

| Off | Tipo | Campo |
|---|---|---|
| 0 | u32 | aos_unix |
| 4 | u16 | duration_s |
| 6 | u16 | tca_offset_s |
| 8 | u8 | sat_index |
| 9 | u8 | max_el_deg |
| 10 | u16 | aos_az_deg |
| 12 | u16 | los_az_deg |
| 14 | u16 | tca_az_deg |

## 3. Protocolo CPS del firmware custom

Puerto serie a 115200 8N1 por el conector Kenwood (UART4).

### Trama

```
A5 FF FF FF <cmd> <len> <payload[len]> <crcH> <crcL>
```

La CRC es CRC-16/CCITT (polinomio 0x1021, inicial 0x0000) sobre los bytes
0..5+len.

### Sesión

1. El PC envía `PROGRAMBT9000U`. A partir de 5 bytes, la radio entra en modo
   programación y responde con `R` + `"RT-950      "`.
2. El PC envía `I`. Si no hay respuesta, aborta sin escribir nada.
3. Para cada sector de 4 KB: `E` + dirección de 3 bytes (big-endian) y la
   radio responde con `E`.
4. Para cada bloque de 128 bytes: `W` + dirección de 3 bytes + datos, y la
   radio responde con `W`.
5. Verificación: `R` + dirección de 3 bytes + longitud, y la radio responde
   con `R` + datos.
6. `K` + u32 Unix en little-endian (pone la hora UTC) y la radio responde
   con `K`.
7. `L` (recargar la base de datos) y la radio responde con `L` + número de
   satélites (0xFF = error).
8. `X` (fin de escritura): la radio vuelve a funcionar con normalidad.

### Comandos añadidos

| Cmd | Petición | Respuesta |
|---|---|---|
| `I` 0x49 | — | `"SAT"`, versión de API, dirección de la DB (3 bytes, BE), tamaño en KB, satélites cargados, origen de la hora |
| `K` 0x4B | u32 Unix (LE) | trama vacía. Se ignora si es anterior a noviembre de 2023 |
| `L` 0x4C | — | 1 byte: número de satélites o 0xFF |

Mientras la sesión CPS está activa, el motor de satélites no lee la flash.

## 4. Configuración `sat_cfg_t` (36 bytes, en 0x0C8000)

`magic` "SCFG", `version`, `selected`, `min_el_deg`, `doppler_on`, `tx_mode`,
`loc_src`, `alert_min`, `rx_wide`, `manual_lat_e6`, `manual_lon_e6`,
`manual_alt_m`, `manual_valid`, reservado y CRC-32 de los bytes 0..31. Si la
CRC no cuadra, se cargan los valores por defecto: Doppler activado, TX por el
mismo chip, Auto, aviso a 2 minutos y RX ancho.

## 5. Para integrarlo en otro programa

RT-950 Toolkit ya lo hace (pestaña Satélites). Para añadirlo a otro programa,
por ejemplo RT-950/950Pro Editor, bastaría con una opción **Tools >
Satellites** que haga lo mismo que `rt950_sat.py`:

1. Descargar TLE.
2. Generar el `satdb.bin` de la sección 2.
3. Si la radio responde a `I`, seguir la sesión de la sección 3.
4. Si no responde (firmware OEM), ofrecer la importación de los canales
   Doppler, que ya es posible con el importador CSV actual.

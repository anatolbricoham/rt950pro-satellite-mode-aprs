# Protocolo de programación y mapa del codeplug — RT-950 / RT-950 Pro

*[English version](PROTOCOL.en.md)*

Lo que implementan `pc/rt950_toolkit/protocol.py` y `codeplug.py`. Las
fuentes son el fichero de modelo del fabricante
(`pc/rt950_toolkit/data/rt950pro_schema.json`), las notas de ingeniería
inversa del firmware abierto (`include/drivers/flash_layout.h`) y
RT-950/950Pro Editor (KK4OXN, MIT), que se ha usado para contrastar cada
detalle (ver D-30).

## 1. Firmware original de Radtel

Puerto serie a 115200 8N1.

### Sesión

| Paso | PC → radio | Radio → PC |
|---|---|---|
| 1 | `PROGRAMBT9000U` (14 bytes ASCII) | `06` |
| 2 | `F` | 16 bytes de identificación |
| 3 | `M` | 12 bytes de modelo, p. ej. `RT-950 Pro` + relleno |
| 4 | `SEND` + 21 bytes (reto) | `06` |
| 5 | bloques de lectura o escritura (abajo) | |
| 6 | `45` (`E`) si se ha escrito, `06` si solo se ha leído | — |

El programa comprueba que el modelo contiene `RT-950` antes de seguir.

### Reto y clave

El reto del paso 4 elige una de 20 claves de 4 bytes. El CPS del fabricante
y el Editor envían siempre el mismo reto:

```
53 45 4E 44 11 10 0F 06 03 01 13 02 13 0E 06 0C 0D 0C 12 04 11 0D 0B 0E 00
```

El byte 4 (`sel`) da la posición del índice:
`idx = ((sel − 0x20)·2 + 1 si sel & 0x20, si no (sel − 0x10)·2) + 1`, y la
clave es `TABLA[reto[4 + idx]]`. Con este reto sale la clave `"RVB "`.

### Cifrado de los datos

Cada byte `b` de datos, con `k = clave[i mod 4]`, se transmite como `b ^ k`
**salvo** si `k == 0x20`, `b == 0x00`, `b == 0xFF`, `b == k` o
`b == k ^ 0xFF`, en cuyo caso va sin cambiar. La misma función cifra y
descifra.

### Bloques

```
lectura:   R  aH aL n           → R aH aL n + n bytes cifrados
escritura: W  aH aL n + datos    → 06
```

`n` es 128 (o el resto al final de una región). Las direcciones son de 16
bits.

La página de APRS se direcciona en el fichero de modelo como 0xFFFF…, pero
se transfiere con comandos propios en la dirección 0:

```
lectura APRS:   T 00 00 80          → T 00 00 80 + 128 bytes
escritura APRS: X 00 00 80 + datos   → 06   (la radio tarda en confirmar: hasta 30 s)
```

### Plan de transferencia

| Región | Dirección | Tamaño | Comandos |
|---|---|---|---|
| channels | 0x0000 | 0x7C00 | R / W |
| vfo | 0x8000 | 0x80 | R / W |
| settings | 0x9000 | 0x80 | R / W |
| dtmf | 0xA000 | 0x180 | R / W |
| modulation (FM/AM/SSB) | 0xB000 | 0x100 | R / W |
| zones | 0xC000 | 0x100 | R / W |
| mod_names | 0xD000 | 0x300 | R / W |
| aprs | (0xFFFF) | 0x80 | T / X en la dirección 0 |

## 2. Firmware custom

Tramas `A5 FF FF FF <cmd> <len> <payload> <crcH> <crcL>` con CRC-16/CCITT y
direcciones de 24 bits sobre la flash SPI (ver
[PROTOCOL_AND_FORMAT.md](../satellite/PROTOCOL_AND_FORMAT.md)). El firmware
responde a `PROGRAMBT9000U` con una trama `A5` en lugar de `06`, y así lo
detecta `protocol.detect()`. Las regiones se escriben con
lectura-modificación-escritura de cada sector de 4 KB para no borrar los
datos vecinos.

## 3. Codificaciones

### Canal (32 bytes, canal `i` en `i × 32`)

| Bytes | Campo | Codificación |
|---|---|---|
| 0–3 | Frecuencia RX | BCD empaquetado, byte menos significativo primero, unidades de 10 Hz. `FF FF FF FF` = canal vacío |
| 4–7 | Frecuencia TX | igual |
| 8–9 | Tono RX | ver abajo |
| 10–11 | Tono TX | ver abajo |
| 12 | Código de señalización | 0–15 (se muestra 1–16) |
| 13 | PTT-ID | 0 OFF, 1 BOT, 2 EOT, 3 BOTH |
| 14 | Potencia / scrambler | bits 0–3: 0 alta, 1 media, 2 baja; bits 4–7: scrambler 0 (OFF) a 8 |
| 15 | Opciones | bit 0 AM, bit 1 TX permitida, bit 2 escaneo, bit 3 bloqueo por ocupado, bits 4–5 cifrado (0–3), bit 6 estrecho, bit 7 aprendizaje FHSS |
| 16–19 | Código FHSS | se conserva tal cual |
| 20–31 | Nombre | GBK (página 936), relleno con `FF` |

### Tonos (2 bytes)

| Valor | Significado |
|---|---|
| `00 00` o `FF FF` | sin tono |
| `n 00`, n = 1–105 | DCS normal: código n de la lista DCS (023, 025, …) |
| `n 00`, n = 106–210 | DCS invertido: código n − 105 |
| otro | CTCSS: frecuencia × 10 en little-endian (67,0 Hz → `9E 02`) |

### Otros campos

| Campo | Codificación |
|---|---|
| Nombres de zona | 10 × 16 bytes en 0xC000, GBK, relleno `FF` |
| Frecuencia del VFO | un dígito por byte: `1 4 5 5 2 5 0 0` = 145,52500 MHz (A en 0x8000, B en 0x8020, C en 0x8040) |
| Desplazamiento del VFO | 7 dígitos en +0x14 |
| Memorias FM | u16 little-endian, MHz × 100 |
| Memorias AM y SSB | u16 little-endian, kHz. Fila SSB de 5 bytes: frecuencia (2), ancho (1), desplazamiento BFO con signo (2) |
| Códigos DTMF | un dígito por byte (0–9, A–D = 10–13, `*` = 14, `#` = 15), relleno `FF` |
| Indicativo APRS | 6 bytes en mayúsculas en APRS+0x11, SSID en APRS+0x17 |
| Ajustes de selección | byte + posiciones de bit según el fichero de modelo |

## 4. Fichero `.dat` del CPS

Es un objeto .NET `KDH.RadioData` serializado con BinaryFormatter
(MS-NRBF). `nrbf.py` lo lee y lo vuelve a escribir byte a byte igual;
`datfile.py` traduce sus listas (canales, nombres de zona, tabla de
funciones, bytes y texto de APRS) al codeplug y al revés. Para guardar se
parte de un `.dat` existente y solo se cambian los valores, de modo que los
campos que el programa no conoce se conservan.

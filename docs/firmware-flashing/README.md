# Actualizar el firmware: aplicación y flasheador web

*[English version](README.en.md)*

El firmware de la RT-950 / RT-950 Pro se cambia por el **cable de
programación** usando el cargador de arranque del fabricante. Hay dos formas:

| | Aplicación (Windows / Linux) | Flasheador web |
|---|---|---|
| Dónde | Pestaña **Firmware** de BricoHams RT-950 Toolkit | `https://anatolbricoham.github.io/rt950pro-satellite-mode/flasher/` |
| Requisitos | El instalador o el paquete | Chrome, Edge u Opera en un ordenador (Web Serial) |
| Firmware | Publicado en las versiones de GitHub (se comprueba el SHA-256) o un `.BTF` propio | Custom de BricoHams y original de Radtel V0.27 servidos por la propia web, o un `.BTF` propio |
| Línea de comandos | `RT950Toolkit flash --port COM5 firmware.BTF [--in-bootloader]` | — |

**Antes de nada lee [Copias de seguridad y aviso de responsabilidad](../BACKUP_AND_DISCLAIMER.md).**
El firmware instalado no se puede leer desde la radio: guarda el `.BTF`
original de tu versión.

## Pasos

1. Batería cargada, cable conectado, ningún otro programa usando el puerto.
2. Elige el firmware (publicado, de la web o desde fichero). El programa
   comprueba que es un firmware de RT-950 y muestra tamaño, bloques y SHA-256.
3. Acepta el aviso y pulsa **Flashear**. La radio pasa a modo cargador, recibe
   el firmware en bloques de 1024 bytes y se reinicia.
4. Si la radio no responde o no arranca: apágala, mantén pulsadas las **dos
   teclas laterales inferiores** al encenderla y vuelve a flashear marcando
   **La radio ya está en modo cargador**.

Para volver al firmware de Radtel, flashea su `.BTF` oficial de la misma
forma.

## Firmware publicados

Cada versión de GitHub incluye `rt950-bricohams-x.y.z.BTF` (con su `.sha256`),
`.bin` y `.hex`. La web copia el `.BTF` de la versión y el original V0.27 en
`/firmware/` junto a `manifest.json` (nombre, tamaño y SHA-256), porque el
navegador solo puede descargar ficheros del mismo sitio que la página.

## Protocolo

Igual que el actualizador del fabricante y `tools/firmware_upload.py`:

```
desde el firmware:  PROGRAMBT9000U -> 06,  UPDATE -> 06   (la radio se reinicia en el cargador)
cargador:           AA cmd argH argL lenH lenL datos.. crcH crcL 55   (CRC-16/CCITT)
                    42 sondeo -> 0A "BOOTLOADER_V3" -> 02 modelo (32 bytes de BTF@0x3E0)
                    -> 04 bloques-1 -> 03 datos (1024 bytes, arg = nº de bloque) -> 45 fin
```

El `.BTF` se envía tal cual; el cargador lo descifra con la clave guardada en
el offset 0x400. Implementaciones: `pc/rt950_toolkit/flasher.py` (aplicación)
y `site/flasher/rt950-flasher.js` (web), probadas contra un emulador del
cargador (`pc/tests/bootloader_emulator.py`, `tests/web/flasher.test.cjs`).

## Estado

El firmware custom es experimental. El flasheo está probado contra el
emulador; el protocolo es el mismo que ya usaba `tools/firmware_upload.py`
con radios reales.

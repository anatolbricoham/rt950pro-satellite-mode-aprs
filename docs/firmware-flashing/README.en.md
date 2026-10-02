# Updating the firmware: application and web flasher

*[Versión en español](README.md)*

The RT-950 / RT-950 Pro firmware is changed over the **programming cable**
through the maker's bootloader. There are two ways:

| | Application (Windows / Linux) | Web flasher |
|---|---|---|
| Where | **Firmware** tab of BricoHams RT-950 Toolkit | `https://anatolbricoham.github.io/rt950pro-satellite-mode/flasher/` |
| Needs | The installer or the package | Chrome, Edge or Opera on a computer (Web Serial) |
| Firmware | Published with the GitHub releases (SHA-256 checked) or your own `.BTF` | BricoHams custom and Radtel stock V0.27 served by the site itself, or your own `.BTF` |
| Command line | `RT950Toolkit flash --port COM5 firmware.BTF [--in-bootloader]` | — |

**First read [Backups and disclaimer](../BACKUP_AND_DISCLAIMER.en.md).** The
installed firmware cannot be read from the radio: keep the original `.BTF` of
your version.

## Steps

1. Battery charged, cable connected, no other program using the port.
2. Pick the firmware (published, from the site or from a file). The program
   checks it is an RT-950 firmware and shows size, blocks and SHA-256.
3. Accept the disclaimer and press **Flash**. The radio switches to the
   bootloader, receives the firmware in 1024-byte blocks and restarts.
4. If the radio does not answer or does not boot: switch it off, hold the
   **two lower side keys** while switching it on and flash again with
   **Radio already in bootloader mode** ticked.

To go back to Radtel's firmware, flash its official `.BTF` the same way.

## Published firmware

Every GitHub release includes `rt950-bricohams-x.y.z.BTF` (with its
`.sha256`), `.bin` and `.hex`. The site copies the release `.BTF` and the
original V0.27 to `/firmware/` with `manifest.json` (name, size and SHA-256),
because the browser can only download files from the same site as the page.

## Protocol

The same as the maker's updater and `tools/firmware_upload.py`:

```
from the firmware:  PROGRAMBT9000U -> 06,  UPDATE -> 06   (the radio restarts into the bootloader)
bootloader:         AA cmd argH argL lenH lenL data.. crcH crcL 55   (CRC-16/CCITT)
                    42 probe -> 0A "BOOTLOADER_V3" -> 02 model (32 bytes from BTF@0x3E0)
                    -> 04 blocks-1 -> 03 data (1024 bytes, arg = block number) -> 45 end
```

The `.BTF` is sent unchanged; the bootloader decrypts it with the key stored
at offset 0x400. Implementations: `pc/rt950_toolkit/flasher.py` (application)
and `site/flasher/rt950-flasher.js` (web), tested against a bootloader
emulator (`pc/tests/bootloader_emulator.py`, `tests/web/flasher.test.cjs`).

## Status

The custom firmware is experimental. Flashing is tested against the
emulator; the protocol is the one `tools/firmware_upload.py` already used
with real radios.

# Backups and disclaimer

*[Versión en español](BACKUP_AND_DISCLAIMER.md)*

A short guide with advice on backing up the **firmware** and the
**configuration** (codeplug) of the Radtel RT-950 / RT-950 Pro before using
BricoHams RT-950 Toolkit, the Android app, the web flasher or the custom
firmware.

## 1. Configuration (codeplug) backup

| Where | How |
|---|---|
| Windows / Linux (Toolkit) | **Radio → Read from radio**. A dated backup is saved automatically in the backups folder (**Radio → Open backups folder**): `%APPDATA%\BricoHamsRT950\backups` on Windows, `~/.config/BricoHamsRT950/backups` on Linux. |
| Android (app) | **Radio** tab → **Read from radio**. The backup appears under **Files → Backups**; ⤓ saves or shares it (Drive, e-mail…). |
| Browser (web) | As on Android; backups are kept in the browser, download them with ⤓. |

Advice:

1. **Read the radio before changing anything** and also keep your own copy:
   **File → Save** (`.rt950`). If you use Radtel's official software, also
   keep a `.dat` (**File → Save as CPS .dat**).
2. Before every write the program reads the radio again and keeps another
   backup (`before-write`). If anything goes wrong: **Radio → Restore a
   backup** (on Android: ↺ on the backup, then **Write to radio**).
3. Keep backups off the computer or phone (USB stick, cloud) now and then,
   especially the first read of a new radio.
4. `.rt950` files are the same on Windows, Linux, Android and the web.

## 2. Firmware backup

- **The installed firmware cannot be read from the radio**: the maker's
  bootloader has no read command. No program can back up the firmware your
  radio is running.
- The firmware backup is **the official `.BTF` file** of your version:
  1. Look up the version in the radio menu (information / version) and note
     it down.
  2. Keep the official `.BTF` of that version: Radtel's web site, or the
     repository's `binary/` folder (V0.15, V0.18, V0.21 and V0.27). The web
     flasher also offers the original V0.27.
- To **go back to the original firmware**: **Firmware** tab → **From file**
  → choose the official `.BTF` → **Flash firmware** (or the web flasher).
- If flashing is interrupted and the radio does not start: switch it off,
  **hold the two lower side keys while switching it on** (bootloader mode)
  and flash again with "Radio already in bootloader mode" ticked.

## 3. Calibration

Factory calibration (power levels, sensitivity, deviation) lives in the
radio's memory. Neither the Toolkit, the app nor the custom firmware change
it. Do not erase the radio's whole memory with other tools.

## 4. Before every operation

- Battery above 50 %.
- Cable firmly inserted (or Bluetooth close to the radio, with no other
  program connected to it).
- Do not disconnect or switch the radio off while writing or flashing.
- Test the custom firmware's transmission into a **dummy load**.

---

## Disclaimer

BricoHams RT-950 Toolkit, the BricoHams RT-950 Programmer app, the custom
firmware, the web flasher and the preloaded codeplugs are provided **"as is",
without warranty of any kind**, under the GPL-3.0 licence. They are not
Radtel products and are not endorsed by the manufacturer.

- Programming the radio or changing its firmware can make it unusable, erase
  its configuration or calibration and void the manufacturer's warranty.
  **You do it at your own risk.**
- The custom firmware is experimental and has not been tested on every
  radio.
- Country codeplugs are built from public data (URE, ukrepeater.net,
  OurAirports) that may be incomplete or out of date. Check them before use.
- Air band, PMR446 and citizens' band frequencies are loaded **for reception
  only**. Transmitting on them with this radio is not allowed. Every user is
  responsible for complying with the regulations of their country and the
  terms of their amateur licence.
- The authors and BricoHams accept no liability for damaged equipment, lost
  data, interference or any other harm arising from the use of this software.

The desktop program, the app and the web flasher show this disclaimer and
ask for it to be accepted before writing to the radio or updating its
firmware.

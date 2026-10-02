<p align="center"><img src="pc/rt950_toolkit/data/brand/bricohams_logo.svg" alt="BricoHams" width="420"></p>

# BricoHams RT-950 — Toolkit, Android app, custom firmware and web flasher

*[Versión en español](README.es.md)* · **Web:** <https://anatolbricoham.github.io/rt950pro-satellite-mode/>

Everything to program and extend the **Radtel RT-950 / RT-950 Pro**, by
**BricoHams** (amateur radio · do it yourself):

| | |
|---|---|
| **BricoHams RT-950 Toolkit** (Windows, Debian/Ubuntu, Arch) | Codeplug editor over the cable for stock and custom firmware, CPS `.dat`, CHIRP and RT-950 Editor CSV, **country codeplugs** (Spain EA1–EA9, United Kingdom), **firmware update**, satellites, HTML help |
| **BricoHams RT-950 Programmer** (Android APK + web) | The same programming over **Bluetooth** from the phone or the browser |
| **Custom firmware** | Satellite mode (SGP4, Doppler split RX/TX), APRS text messaging, BricoHams boot banner |
| **Web flasher** | Firmware update from Chrome/Edge (Web Serial), including going back to the stock firmware |

> **Derived work.** Based on the open-source firmware by
> [Hertzz58/Radtel-RT950-Pro-Firmware](https://github.com/Hertzz58/Radtel-RT950-Pro-Firmware)
> (GPL-3.0; full history preserved). The programming protocol was
> cross-checked with RT-950/950Pro Editor (KK4OXN, MIT) and the Bluetooth
> protocol is documented by rt950-ble (bartasx, MIT).
>
> **Status:** verified on the host (real firmware code, radio and bootloader
> emulators, simulated Bluetooth), **not yet on real hardware**. Read
> [Backups and disclaimer](docs/BACKUP_AND_DISCLAIMER.en.md) first.

## Downloads

Every [release](https://github.com/anatolbricoham/rt950pro-satellite-mode/releases)
contains the signed Windows installer and portable executable, the `.deb`,
the Arch package, a portable Linux executable, the Android APK, the firmware
(`.BTF/.bin/.hex`), the Spain and UK codeplugs, the signing certificate and
`SHA256SUMS`, all built by GitHub Actions.

## Documentation

| | English | Español |
|---|---|---|
| Backups and disclaimer | [BACKUP_AND_DISCLAIMER.en.md](docs/BACKUP_AND_DISCLAIMER.en.md) | [BACKUP_AND_DISCLAIMER.md](docs/BACKUP_AND_DISCLAIMER.md) |
| Toolkit (desktop) | [docs/toolkit/README.en.md](docs/toolkit/README.en.md) | [docs/toolkit/README.md](docs/toolkit/README.md) |
| Country codeplugs | [docs/country-codeplugs/README.en.md](docs/country-codeplugs/README.en.md) | [docs/country-codeplugs/README.md](docs/country-codeplugs/README.md) |
| Android / web app | [docs/android/README.en.md](docs/android/README.en.md) | [docs/android/README.md](docs/android/README.md) |
| Firmware update | [docs/firmware-flashing/README.en.md](docs/firmware-flashing/README.en.md) | [docs/firmware-flashing/README.md](docs/firmware-flashing/README.md) |
| Installers, packages, signing | [docs/signing-and-packages/README.en.md](docs/signing-and-packages/README.en.md) | [docs/signing-and-packages/README.md](docs/signing-and-packages/README.md) |
| Satellite mode | [docs/satellite/README.en.md](docs/satellite/README.en.md) | [docs/satellite/README.md](docs/satellite/README.md) |
| APRS messaging | [docs/aprs-messaging/README.en.md](docs/aprs-messaging/README.en.md) | [docs/aprs-messaging/README.md](docs/aprs-messaging/README.md) |
| Programming protocol | [docs/toolkit/PROTOCOL.en.md](docs/toolkit/PROTOCOL.en.md) | [docs/toolkit/PROTOCOL.md](docs/toolkit/PROTOCOL.md) |
| Design decisions (D-01…D-52) | [DESIGN_DECISIONS.en.md](docs/satellite/DESIGN_DECISIONS.en.md) | [DESIGN_DECISIONS.md](docs/satellite/DESIGN_DECISIONS.md) |
| Tests | [TESTING.en.md](docs/satellite/TESTING.en.md) | [TESTING.md](docs/satellite/TESTING.md) |
| User manual (HTML, also in the app with F1) | [help/en](pc/rt950_toolkit/help/en/index.html) | [help/es](pc/rt950_toolkit/help/es/index.html) |

| Desktop | Android |
|---|---|
| ![Toolkit](docs/toolkit/img/en-channels.png) | ![App](docs/android/img/channels.png) |

---

Open-source bare-metal firmware for the **Radtel RT-950 Pro** handheld radio.

**Target**: Artery AT32F403A (ARM Cortex-M4F @ 120 MHz, 1 MB flash, 96 KB SRAM)

## Status

> [!WARNING]
> This is heavily work-in-progress and still is ridden with bugs. Early hardware bring-up. Boot screen, LEDs, LCD, and GPIO confirmed working on real hardware.

### Hardware Confirmed

- **LCD**: ST7789V 240x320 via 8080 parallel bus (PD0-PD15), custom text rendering
- **LEDs**: Red (PC13), Green (PC14)
- **Backlight**: Primary (PC6), Secondary (PB3)
- **Power latch**: PB9
- **Band relay**: PC4
- **Boot**: Custom firmware loads and runs via OEM bootloader

### What's Implemented (Roughly Code-Complete, Untested)

- **Hardware drivers**: GPIO, SPI2 (flash), bit-bang SPI (BK4829), bit-bang I2C (SI4732), USART1/3/UART4, ADC2, DAC1+TIM6+DMA, ST7789V LCD (8080 parallel), DMA1/DMA2
- **RF control**: Dual BK4829 transceiver driver, frequency programming, TX power levels, calibration tables from flash
- **Application layer**: Dual VFO (A/B/C), PTT with active-low relay control, keypad scanner with debounce, rotary encoder, S-meter, menu system, frequency entry
- **Digital modes**: APRS (hardware FSK via BK4829, MIC-E packets), DTMF encode/decode, CTCSS/DCS tone generation
- **Receivers**: SI4732 AM/FM/SSB broadcast radio, FM broadcast with presets, weather channels
- **Data**: SPI flash layout (990 channels, VFO configs, settings), wear-leveled NV storage, CPS programming protocol (UART4)
- **Comms**: GPS NMEA parser, Bluetooth BLE data bridge, VOX, NOAA weather radio (SI4732 WB)
- **UI**: Display rendering, splash screen, zone browser, text input, scanner, spectrum analyzer, hierarchical 12-category menu
- **Radio**: Cross-band repeat (A->B, B->A, duplex), band-specific relay routing, interrupt-driven UART
- **Safety**: PTT relay polarity verified (active-low), PA gate timing, TX band limits
- **Tools**: BTF firmware encrypt/decrypt, CPS flash read/write, firmware upload with auto-restart

### Satellite Mode (new)

Amateur satellite tracking with Doppler-corrected split operation, modelled on
OpenGD77's satellite screen and AnyTone's satellite mode: on-radio SGP4,
next-pass prediction, polar plot, VFO A = downlink RX / VFO B = uplink TX,
SO-50 arming tone, GPS/PC UTC clock and a satellite icon in the status bar.
RT-950 Toolkit (or `tools/rt950_sat.py`) downloads TLEs, predicts passes,
uploads everything to the radio and also generates Doppler channels for radios
still running the **stock** firmware.

![Satellite screens](docs/satellite/img/pantallas.png)

### APRS Messaging (new)

APRS 1.0.1 text messages over the BK4829 AFSK path: inbox of 24 messages,
compose with multi-tap and quick texts, automatic acks, retries at 30/60/120 s,
duplicate filter, bulletins and a `MSG` indicator. Menu APRS Set → Messages or
PF action 9.

![APRS messaging screens](docs/aprs-messaging/img/screens.png)

### Country codeplugs (new in 0.3.0)

One click builds a ready-to-use configuration for a new radio or adds it to
the existing one (overwrite or append): **Spain** with one zone per district
(EA1…EA9) holding amateur repeaters and nearby airports (RX only) and a last
zone with PMR446 and CB; **United Kingdom** with the ETCC regions, simplex
and PMR446/CB. See [docs/country-codeplugs](docs/country-codeplugs/README.en.md).

### BricoHams RT-950 Toolkit

Cross-platform desktop application and command line (`pc/`): read, write and
verify the radio (stock and custom firmware), channels, zones, VFO, settings,
DTMF, FM/AM/SSB, APRS, CPS `.dat` files, CHIRP and RT-950 Editor CSV, stock
channel sets, automatic backups, satellites, boot logo, country codeplugs,
firmware update and an HTML manual (F1).

![RT-950 Toolkit](docs/toolkit/img/en-channels.png)

### What's Not Done

- Most peripherals untested on hardware (SPI flash, BK4829, SI4732, GPS, keypad, encoder)
- Bluetooth audio streaming (AT commands only)
- Voice prompt audio samples (tone patterns only)

### Example Hardware Test

![Example Test](docs/screenshots/screenshot_1.jpg)

## Building

### Prerequisites

- `arm-none-eabi-gcc` (12.x or later)
- `python3` with `pyserial`
- `make`

### Build

```bash
make                  # Build release firmware
make DEBUG=1          # Build with debug UART output
make btf              # Generate encrypted .BTF for upload
make btf DEBUG=1      # Generate debug .BTF
make size             # Print flash/RAM usage
make clean            # Remove build artifacts
make test-host        # Host tests: satellites, APRS messaging, RT-950 Toolkit
```

### Upload to Radio

```bash
# Normal upload (auto-enters bootloader via PROGRAMBT9000U handshake)
python3 tools/firmware_upload.py upload /dev/ttyUSB0 build/rt950-custom.BTF -v

# Radio already in bootloader (side buttons held at power-on)
python3 tools/firmware_upload.py upload /dev/ttyUSB0 build/rt950-custom.BTF --ptt -v

# Reset back to default firmware
python3 tools/firmware_upload.py upload /dev/ttyUSB0 binary/RT_950Pro_V0.27_260203/RT_950Pro_V0.27_260203.BTF --ptt -v
```

### Hardware Tests

```bash
make test TEST=1   # Backlight blinky
make test TEST=2   # UART echo (GPS + BT)
make test TEST=3   # LCD color bars
make test TEST=4   # BK4829 chip ID read
make test TEST=5   # SI4732 revision read
make test TEST=6   # SPI flash JEDEC ID
make test TEST=7   # ADC battery + audio
make test TEST=8   # DAC 1 kHz tone
make test TEST=9   # Keypad + encoder scan
make test TEST=10  # GPS NMEA display
make test TEST=11  # Full system diagnostic
```

### Output Files

| File | Description |
|------|-------------|
| `build/rt950-custom.elf` | Linked ELF with debug symbols |
| `build/rt950-custom.bin` | Raw binary for flashing |
| `build/rt950-custom.hex` | Intel HEX for flashing |
| `build/rt950-custom.BTF` | Encrypted firmware for upload |

## Memory Layout

```
Flash:
  0x08000000  OEM Bootloader (12 KB, permanent)
  0x08003000  Custom firmware starts here <- linker ORIGIN
  0x08100000  End of flash (1 MB)

SRAM:
  0x20000000  .data + .bss + heap
  0x20017BB0  Stack top (matches OEM)
  0x20018000  End of SRAM (96 KB)
```

The bootloader only checks that `[0x08003000]` is a valid SRAM address.
**No CRC, no signature** - custom firmware is flashable via the standard OEM update tool.

## Tools

| Tool | Description |
|------|-------------|
| `tools/firmware_upload.py` | Upload .BTF firmware to radio via serial |
| `tools/encrypt_btf.py` | Encrypt/decrypt BTF firmware files |
| `tools/cps_flash.py` | Read/write radio configuration via CPS protocol |
| `tools/rt950_sat.py` | Satellite tool: TLE download, pass prediction, Doppler channels CSV, satellite DB upload |
| `pc/run_toolkit.py` | BricoHams RT-950 Toolkit: codeplug editor, country codeplugs, firmware, satellites (see [docs/toolkit](docs/toolkit/README.en.md)) |
| `mobile/` | BricoHams RT-950 Programmer: Android APK / web app over Bluetooth (see [docs/android](docs/android/README.en.md)) |
| `site/` | Project web site and web flasher (GitHub Pages) |
| `tools/build_country_data.py` | Builds the country codeplug data from the public lists in `tools/country_sources/` |

See [tools/README.md](tools/README.md) for detailed usage and protocol documentation.

### Known BTF Encryption Keys

| Version | Key (hex) |
|---------|-----------|
| V0.15 | `71CAEFACD047EF83EFD2141A3512A638` |
| V0.18 | `DC24DEF7CF4F3FED91BCC88BB0613A51` |
| V0.21 | `3C0F640BB03230BB97AF8029C4AD794D` |
| V0.27 | `7E807B1761A4EBC6FC3A8DD33752F305` |

Keys are stored at offset 0x400 in the `.BTF` file. Use `--auto` to extract automatically.

## Key Notes

**GPIO registers**: AT32F403A uses SCR at +0x10 (set pin HIGH) and CLR at +0x14 (clear pin LOW). The OEM GPIO helpers are at 0x080155B2 (set) and 0x080155AE (clear).

**PTT relays are active-low**: All relay pins SET HIGH = deactivated (RX/idle). Pins are CLEARED to activate TX path. Wrong polarity = PA damage.

**SPI1 is unused**: Despite being a standard SPI peripheral, the OEM firmware never configures SPI1. PA7 (SPI1_MOSI) is actually the keypad latch line.

**Dual BK4829 RF chips**: Chip 0 (CS=PE8) handles main VFO, chip 1 (CS=PE15) handles sub-receiver and APRS. They share clock (PE10) and data (PE11) lines.

**Unused peripherals**: SPI1, ADC1, USART2, I2C1/I2C2 hardware, DMA2 CH1 have zero references in the OEM binary.

## Contributing

Please feel free to contribute, we will be working through the hardware testing and implementation but if you have helpful information or would like to make pull requests, please do! Feel free to make PRs or open a discussion.

## License

This project is for educational and amateur radio experimentation purposes.
See [LICENSE](LICENSE) for details.

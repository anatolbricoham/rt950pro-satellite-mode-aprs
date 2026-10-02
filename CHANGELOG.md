# Changelog

All notable changes to the RT-950 Pro custom firmware are documented here.

## [0.3.0] - 2026-10-02

BricoHams branding, country codeplugs for Spain and the United Kingdom,
firmware update from the application and the web, Android app, signed
installers and Linux packages.

### Added
- Branding: the project and the desktop application become **BricoHams
  RT-950 Toolkit**; logos, icons, header, About dialog with credits;
  BricoHams boot banner in the firmware (`src/app/splash.c`) and a BricoHams
  boot logo for the stock firmware
- HTML user manual in Spanish and English (`pc/rt950_toolkit/help/`, F1),
  also published on the web site
- Backup guide and disclaimer (`legal.py`, `docs/BACKUP_AND_DISCLAIMER*.md`):
  shown on first run and required before writing or flashing
- Country codeplugs (`country.py`, `data/countries/{es,gb}.json`,
  `tools/build_country_data.py`): Spain EA1–EA9 with amateur repeaters (URE)
  and nearby airports (OurAirports, 8.33 kHz designators converted to the real
  frequency) plus a PMR446/CB zone; UK ETCC regions, simplex and PMR446/CB;
  zones trimmed by distance to 99 channels; overwrite or append; airband, PMR
  and CB channels are RX only. GUI dialog with preview and CLI `country`
  command; exports to `.rt950`, CHIRP and RT-950 Editor CSV
- Firmware update (`flasher.py`, `firmware.py`, Firmware tab, CLI `flash`):
  OEM bootloader protocol, model check, firmware published with the GitHub
  releases downloaded with SHA-256 check
- Web site on GitHub Pages (`site/`, `tools/build_site.py`): home page,
  manual, firmware, codeplugs and a **web flasher** (Web Serial)
- **BricoHams RT-950 Programmer** (`mobile/`): Android APK (Capacitor) and web
  app over Bluetooth LE or cable; read/write with differential writes,
  channels, zones, settings, APRS, country codeplugs, backups, files
- Packaging: Windows installer (Inno Setup, Spanish/English, no admin rights)
  and portable executable signed with the BricoHams certificate; `.deb` for
  Debian/Ubuntu with udev rule; Arch Linux `PKGBUILD`; signing pipeline ready
  for SignPath or Azure Trusted Signing
- Tests: bootloader emulator, country codeplug tests, web core/flasher/native
  Bluetooth tests under Node, end-to-end test of the web app in Chromium
- Docs: country codeplugs, Android, firmware flashing, signing and packages;
  design decisions D-43 to D-52

### Changed
- Writing a configuration only sends the regions that are complete in the
  file (`valid_regions`); partial codeplugs are merged with a fresh read or
  the last backup so settings are never blanked
- Writing channels also writes zones (the nonexistent `ext` region was
  removed)
- Settings and backups move to the `BricoHamsRT950` folder
- CI builds every package, the APK and the site and attaches everything,
  with `SHA256SUMS`, to the release

## [0.2.0] - 2026-10-02

APRS messaging in the firmware and RT-950 Toolkit for Windows and Linux.
Documentation in Spanish and English (`*.md` / `*.en.md`).

### Added
- APRS text messaging (`aprs_msg_codec.c`, `aprs_msg.c`, `aprs_msg_ui.c`):
  APRS 1.0.1 messages, acks, rejects and bulletins; 24-message inbox;
  retries at 30/60/120 s; automatic ack with random delay; duplicate filter;
  compose screen with multi-tap and quick texts; `MSG` status indicator;
  menu APRS Set → Messages / Msg RX / Auto ACK / Msg Retries; PF action 9;
  settings at SPI 0x0C9000
- `aprs.c`: `aprs_send_frame()`, raw information field on RX handed to the
  messaging engine
- RT-950 Toolkit (`pc/rt950_toolkit`, `pc/run_toolkit.py`): desktop app and
  CLI; read/write/verify for stock (OEM protocol) and custom firmware;
  channels, zones, VFO, settings, DTMF, FM/AM/SSB, APRS; `.rt950`, CPS `.dat`,
  raw image, native/CHIRP/RT-950 Editor CSV (channels, zones, FM/AM/SSB);
  stock channel sets; automatic backups and restore; satellites tab;
  boot logo; English/Spanish interface
- Radio emulator (`pc/tests/oem_emulator.py`), toolkit unit tests, GUI smoke
  test, APRS messaging host test (`tests/test_aprs_msg.py`)
- GitHub Actions: firmware build, host tests, Windows and Linux executables,
  releases on `v*` tags
- Docs: `docs/toolkit/` (user guide, protocol), `docs/aprs-messaging/`,
  English versions of the satellite docs, design decisions D-29 to D-42

### Changed
- `tools/rt950_sat.py` is now a wrapper around `pc/rt950_toolkit/sat.py`;
  `sat_freqs.json` moved to `pc/rt950_toolkit/data/`
- `tools/rt950_sat_gui.py` removed: its functions are in the RT-950 Toolkit
  Satellites tab
- APRS settings loader follows the CPS layout of the APRS page

## [0.1.0-sat] - 2026-10-02

Satellite mode (see docs/satellite/).

### Added
- SGP4 near-Earth propagator in double precision with self-contained math
  kernels (`sat_math.c`, `sat_sgp4.c`); verified against python-sgp4/Skyfield
- Incremental AOS/TCA/LOS pass predictor (`sat_pred.c`)
- Satellite engine (`satellite.c`): SPI-flash database at 0x0C0000, UTC clock
  (GPS RMC / PC / manual), observer from GPS or database QTH, background
  predictions, Doppler-corrected VFO A (RX) / VFO B (TX), PTT hooks, SO-50
  arming tone, AOS alert
- Satellite screens (`sat_ui.c`): list, tracking with polar plot, pass list;
  status-bar satellite icon; "Satellite" menu category; PF action 8 and
  long-press D shortcut
- CPS extension commands `I` (info), `K` (set UTC), `L` (reload satellite DB)
- GPS: RMC date parsing
- Input event dispatcher in `main.c` (key, long-press, encoder events)
- PC tool `tools/rt950_sat.py` + GUI + `sat_freqs.json`: TLE download, pass
  report (HTML/CSV/ICS), CHIRP CSV Doppler channels for the stock firmware,
  `satdb.bin` build and upload
- Host test-suite `make test-host` (SGP4, predictor, pty end-to-end, UI render)

### Fixed
- `cps.c`: payload buffer overflow on full 128-byte writes (3 address bytes)

## [0.0.3] - 2026-04-06

First working speaker output, audio architecture hardware-verified.

### Audio
- DAC tones confirmed through speaker (V12 diagnostic: 6/6 tests pass)
- PE4 = amp power rail, PB8 = amp enable, PE1 = mute, PC12 = audio mux
- BK4829 R47/R48 must be zeroed before DAC playback (AF bus loading)
- Amp power-cycle sequence required for reliable cold-start audio
- PC12 polarity discrepancy: OEM SETs HIGH for beep, custom FW needs LOW

### Bootloader
- Decryption algorithm verified correct (encrypt_btf.py confirmed)
- UART update protocol fully decoded; bootloader assembly annotated

### Bug Fixes
- PE4 was cleared "for safety" - actually cuts amplifier power
- gpio.h comments had swapped OEM addresses/offsets (code was correct)
- HardFault workaround: debug builds skip SPI flash in vfo_load_state
- Assembly fix: 0x080038EA mislabeled PC10, corrected to PC12

## [0.0.2] - 2026-04-04

Hardware validation release. All keypad/encoder/PTT inputs confirmed working
on real hardware. Partial menu rendering tested. Audio playback under active
debugging.

### Hardware Confirmed
- All keypad buttons mapped and tested (4x5 matrix, PC0-3 cols, PD0-7 rows)
- Rotary encoder (PB4/PB5) confirmed working
- PTT1 (PE3), PTT2 (PE2), EXT_PTT (PE5), Side Key (PA12) all functional
- Power button (PE0) input confirmed
- DAC1 (PA4) outputting valid sine waves via TIM6+DMA2 (registers verified)
- SPI flash detected: Macronix MX25L1606E (JEDEC 0x00C22016)
- Wear-leveling probe reads calibration data from flash

### Bug Fixes
- Fixed HardFault during boot: WL probe buffer overflow (4 → 160 bytes)
- Fixed HardFault handler: naked ASM trampoline preserves stacked registers
- Fixed hex debug output corruption: arithmetic conversion immune to BTF .rodata issues
- Fixed PTT1/side buttons: PE0/2/3/5 and PA12 configured as inputs with pull-ups
- Fixed DAC DMA underrun: deferred EN1+DMAEN1 until DMA armed, clear DMAUDR1

### Display
- Partial menu rendering test (renders but not yet functional, visual glitches present)
- Boot splash screen with status text

### Repository
- V0.27 annotated disassembly moved to `assembly/` directory

### Known Limitations
- Audio amplifier not yet producing sound (DAC registers verified correct, analog path debugging in progress)
- Battery ADC reads zero after first sample
- SI4732 SSB patch binary not extracted from OEM flash
- Bluetooth audio streaming not implemented (AT commands only)

## [0.0.1] - 2026-04-03

Initial public release. Custom bare-metal firmware boots and runs on the
Radtel RT-950 Pro with LCD output, LED control, and OEM bootloader upload.

### Hardware Confirmed
- LCD ST7789V init and pixel writes via 8080 parallel bus (PD0-PD15)
- Embedded 8x8 bitmap font with 1x and 2x text rendering
- Boot screen with status display on 240x320 IPS panel
- Red LED (PC13) and Green LED (PC14) GPIO control
- LCD backlight primary (PC6) and secondary (PB3)
- Power latch (PB9) and band relay (PC4)
- Firmware upload via OEM bootloader (custom BTF encryption)
- Debug UART output for hardware bring-up

### Firmware
- Complete bare-metal C firmware for AT32F403A (Cortex-M4F @ 120 MHz)
- 94 source files (~19,600 lines of C) across include/ and src/
- Linker script with 12 KB bootloader reservation (ORIGIN=0x08003000)
- SystemInit: 120 MHz PLL (8 MHz HEXT x15), SysTick, IWDG watchdog

### Drivers (Roughly Code-Complete, Mostly Untested)
- BK4829 dual RF transceiver (bit-bang SPI, PE8/PE10/PE11/PE15)
- ST7789V LCD (8080 parallel bus, software bit-bang)
- SI4732 AM/FM/SSB/WB receiver (bit-bang I2C, PB6/PB7)
- W25Q16 SPI flash with wear-leveling (hardware SPI2, PB12-PB15)
- UART: Bluetooth (USART1 115200), GPS (USART3 9600), CPS (UART4 115200)
- ADC2 (PA0 VOX, PA1 battery), DAC1+TIM6+DMA tone generation
- GPIO with verified pin map (56 pins mapped, binary-verified + hardware-probed)

### Application (Roughly Code-Complete, Untested)
- Dual VFO (A/B/C), 990 memory channels with zone browsing
- APRS via BK4829 hardware AFSK (MIC-E encoding)
- DTMF encode/decode with contacts
- GPS NMEA parsing, FM/AM broadcast radio
- NOAA weather radio (7 channels via SI4732 WB)
- Channel scanner, spectrum analyzer
- Cross-band repeat (A->B, B->A, duplex), VOX
- Hierarchical 12-category menu (43 items)
- CPS wireless programming via Bluetooth

### Tools
- `firmware_upload.py` - Upload .BTF firmware with auto-restart and flood probe
- `encrypt_btf.py` - BTF encryption/decryption (keys for V0.15/V0.18/V0.21/V0.27)
- `cps_flash.py` - Read/write radio configuration via CPS serial protocol
- 11 hardware test modes (backlight, UART, LCD, BK4829, SI4732, flash, ADC, DAC, keypad, GPS, full diagnostic)

### Reverse Engineering
- Full V0.27 OEM binary disassembly (216K lines, radare2)
- OEM bootloader disassembly (4.7K lines, 95 functions)
- 80+ OEM function addresses mapped to C source equivalents
- GPIO pin map cross-referenced: binary analysis + hardware probing
- BTF encryption algorithm fully reversed (XOR cipher with bit-rotation key expansion)
- CPS, Bluetooth, bootloader, and KDH cloud protocols documented

### Known Limitations
- Most peripherals untested on hardware (SPI flash, BK4829, SI4732, GPS, keypad, encoder)
- SI4732 SSB patch binary not extracted from OEM flash
- SI4732 XOSCEN configuration needs PCB crystal verification
- Bluetooth audio streaming not implemented (AT commands only)
- Voice prompt audio samples not implemented (tone patterns only)

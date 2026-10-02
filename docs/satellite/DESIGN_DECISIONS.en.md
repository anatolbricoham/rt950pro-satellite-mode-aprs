# Design decisions — BricoHams RT-950: satellites, Toolkit, APRS, countries, firmware and Android

*[Versión en español](DESIGN_DECISIONS.md)*

Every decision follows the format **Context → Decision → Alternatives →
Consequences**. They are numbered (D-xx) so they can be cited from code,
issues and commits.

---

## Scope and architecture

### D-01 · Two ways: stock firmware and custom firmware

**Context.** The RT-950 Pro open firmware is alpha: LCD, keypad and audio
verified; BK4829, GPS and flash untested on hardware. Most users run Radtel's
firmware, which cannot be modified.

**Decision.** Two ways that share the tool and the frequency database:

- **A**: split Doppler channels for the stock firmware.
- **B**: real SGP4 tracking in the custom firmware.

**Alternatives.** Custom firmware only (useless today for almost everyone)
or channels only (no real tracking and no icon).

**Consequences.** The work is useful from day one and way B can mature
together with the base firmware.

### D-02 · Own PC tool instead of modifying RT-950/950Pro Editor

> Revised in D-29: the satellite tool is now part of RT-950 Toolkit, which
> also reproduces the Editor's functions.

**Context.** The Editor's repository (KK4OXN, MIT) only publishes binaries.
Its source code is not available. The Editor does import CHIRP-compatible
CSVs.

**Decision.** Create `tools/rt950_sat.py`, which generates a CHIRP CSV for
the Editor (way A) and talks directly to the custom firmware (way B). The
`satdb.bin` format and the protocol are documented in
[PROTOCOL_AND_FORMAT.en.md](PROTOCOL_AND_FORMAT.en.md) so the Editor's author
can integrate them.

**Alternatives.** Modifying the Editor: without source code it cannot be
maintained (see D-30 for how it was used as a reference).

**Consequences.** No third-party dependency. Integration into the Editor can
be proposed as a feature request with this specification.

### D-03 · Orbit computation on the radio, plus a PC pass table

**Context.** The RT-950 Pro has GPS. OpenGD77 computes orbits on the radio.
A precomputed pass table goes stale and is only valid for one QTH.

**Decision.** The radio propagates with SGP4 from the TLEs: position,
Doppler and prediction. The PC adds a pass table as a shortcut, used when the
current QTH is within 25 km of the file's QTH and the mask matches.

**Alternatives.** PC table only (fails when travelling, and real-time
Doppler needs the orbit anyway) or radio computation only (more CPU at
start-up).

**Consequences.** Works portable with GPS and starts quickly at home. PC and
radio use the same algorithm, so results agree to the second (verified).

---

## Orbit computation

### D-04 · SGP4 near-Earth only (no SDP4)

**Context.** All FM satellites and the V/U linear ones are LEO (90 to
115 minute period). SDP4 adds about 6 KB of code and a lot of complexity.

**Decision.** Port only Vallado's near-Earth branch (*Revisiting Spacetrack
Report #3*, "improved" mode, WGS-72). Objects with a period of 225 minutes or
more are flagged `SAT_FLAG_DEEP_SPACE` and rejected with
`SGP4_ERR_DEEPSPACE`.

**Consequences.** Small, verifiable code. A future HEO satellite would need
SDP4.

### D-05 · Double precision with an own maths library

**Context.** The Cortex-M4F only has a single-precision FPU. SGP4 in `float`
accumulates errors of several km and several tenths of a degree. The firmware
links with `-nostdlib`, without libm.

**Decision.** Use `double` (emulated by libgcc, linked with `-lgcc`) and an
own maths library, `sat_math.c`, with fdlibm polynomials for sin/cos/atan,
Cody-Waite reduction and Newton for sqrt and cbrt.

**Alternatives.** Link newlib libm (pulls in `errno`, `reent` and more size)
or use `float` (not precise enough).

**Consequences.** Relative error of 4·10⁻¹⁶ against libm. One propagation
costs about 1 ms on the AT32 (estimated), which leads to decision D-08. It
takes about 14 KB of flash.

### D-06 · WGS-72 for propagation, WGS-84 for the observer

**Decision.** Propagation constants are WGS-72, because TLEs are generated
with that model and using another one adds error. The observer sits on the
WGS-84 ellipsoid, which is what GPS provides.

### D-07 · TEME → ECEF with GMST only (no polar motion or dUT1)

**Context.** Amateur precision does not need the full IERS chain.

**Decision.** Rotate with GMST (IAU-82) and treat UTC ≈ UT1.

**Consequences.** Error against Skyfield (full model): 0.003° in elevation
and 0.23 m/s in range rate, i.e. **0.3 Hz at 437 MHz**. Negligible.

### D-08 · Incremental prediction with a per-tick budget

**Context.** The scheduler is cooperative and the IWDG resets the radio if a
task blocks. Finding a pass costs 120 to 450 propagations (about 190 on
average, measured).

**Decision.** `sat_pred_step()` advances at most 12 propagations per call.
`sat_poll()` runs every 100 ms, so it uses at most 12 % CPU, and only while
predictions are pending.

**Consequences.** The user interface always responds. With 48 satellites the
first full prediction takes about 70 seconds, and only when the PC table
cannot be used.

### D-09 · Pass search algorithm

**Decision.**

- Coarse step by elevation: 240 s below −40°, 120 s below −20°, 45 s below
  −8° and 20 s near the horizon.
- 1 s bisection for AOS and LOS.
- 10 s step and ternary search for TCA.
- If the satellite is already above the horizon at the start, AOS is searched
  backwards.

**Consequences.** Matches Skyfield to the second on every pass of the test
bench, including a 0° grazer. Grazers shorter than about 20 s can be missed;
they cannot be worked anyway.

---

## Data and storage

### D-10 · Database in SPI flash at 0x0C0000 (32 KB) and 0x0C8000 (config)

**Context.** The OEM boot image goes up to 0x0B5800 and the first OEM font
starts at 0x15C000. The OEM CPS and the Editor use 16-bit addresses
(0x0000–0xFFFF).

**Decision.** Reserve `SAT_DB` at 0x0C0000–0x0C7FFF and `SAT_CFG` at
0x0C8000–0x0C8FFF.

**Consequences.** Neither the OEM CPS nor the Editor can overwrite these
areas, and going back to the OEM firmware does not touch them. 48 satellites
and 1024 passes fit. A `_Static_assert` guarantees it.

### D-11 · Binary format with fixed records, decoded TLEs and CRC-32

**Decision.** 64-byte header, 128-byte satellite records with the elements as
`double`, and 16-byte pass records. All little-endian, no padding. Header and
payload CRC-32 and a format version.

**Alternatives.** TLEs as text (needs a parser on the radio and is more
fragile) or a variable-size format.

**Consequences.** The radio only copies structs. A `_Static_assert` in C and
an `assert` in Python keep the format in sync.

### D-12 · Time source: GPS, then PC, then manual

**Decision.** Software clock (UTC base + `get_tick()`).

- **GPS**: an RMC sentence with date synchronises as soon as it arrives. Date
  parsing was added to `gps.c`.
- **PC**: CPS command `K` when uploading data.
- **Jumps**: if the time changes by more than 60 s, predictions are
  invalidated.

**Consequences.** The oscillator drifts a few seconds a day without GPS.
Until there is a valid time, the interface shows `NO UTC TIME` and does not
predict.

### D-13 · Position in Auto mode: GPS or the file's QTH

**Decision.** Three modes: Auto (GPS with fix, otherwise the file's QTH or
manual), GPS only, and DB/manual. The file's QTH comes from the locator given
on the PC (IM98IB by default). It is only recomputed when moving more than
1 km, and predictions are invalidated when moving more than 10 km.

---

## Radio control

### D-14 · VFO A = downlink RX, VFO B = uplink TX; TX on the same chip by default

**Context.** There are two BK4829s, but they share the antenna, band relays
and the PA T/R switching. It is not verified that the RT-950 Pro can receive
on one chip while the other transmits.

**Decision.** VFO A follows the downlink and VFO B shows the uplink. By
default (`TX VFO = Same(A)`), PTT transmits the uplink **on VFO A's chip**
(split, like OpenGD77), which is safe. With `TX VFO = VFO B` VFO B's chip
transmits and A stays in RX (AnyTone style). This option is experimental
until verified on hardware.

**Consequences.** Safe default behaviour, and the architecture is ready for
full duplex if the hardware allows it.

### D-15 · RX Doppler every second; TX fixed for each over

**Context.** `bk4829_set_frequency()` rewrites `REG_30 = 0xBFF1` (RX enable).
Calling it in the middle of a transmission would cut it.

**Decision.**

- **RX**: corrected once per second, quantised to 100 Hz, and the chip is
  only touched if the value changes.
- **TX**: computed when PTT is pressed and held until release.

**Consequences.** In a 20 s over, the 435 MHz uplink drifts at most about
2 kHz near TCA, within the tolerance of a satellite FM receiver. On VHF the
drift is three times smaller.

### D-16 · Never transmit on RX-only or linear satellites

**Decision.** `sat_ptt_prepare()` returns −1 if the satellite has no valid FM
uplink, and `radio_ptt_on()` aborts. The BCL and calibrated band limit checks
are kept and release the satellite state if they fail.

### D-17 · One-shot arming tone

**Decision.** `*` arms the next transmission with `arm_tone` (74.4 Hz on
SO-50) and disarms by itself on PTT release. That is how operators use it:
2 s with 74.4 Hz, then 67.0 Hz.

### D-18 · Satellite mode leaves no trace

**Decision.** On entry, copies of VFO A and B are saved and dual watch, which
would steal the receiver, is paused. On exit, frequency, offset, tones, DCS,
bandwidth and modulation are restored.

**Consequences.** Verified in the simulator: the VFOs return exactly to their
previous values.

---

## User interface

### D-19 · Direct drawing, 1 Hz refresh and built-in fonts

**Context.** There is no frame buffer and the LCD is written over a
software-emulated 8080 bus.

**Decision.** Full redraw only when the screen changes. The rest refreshes
once per second, on a key press or at the start or end of a transmission. The
built-in fonts (5×7 and 8×8) are used instead of the OEM flash fonts, to not
depend on them.

**Consequences.** No visible flicker and little bus use.

### D-20 · English text on the radio

**Decision.** Chosen by the user. Consistent with the rest of the firmware
and with OpenGD77 (AOS, LOS, TCA, AZ, EL). The documentation is in Spanish
and English, and RT-950 Toolkit has a bilingual interface (D-37). The PC pass
report is in Spanish.

### D-21 · Shortcuts and icon

**Decision.**

- **Shortcuts**: menu category 13 (`Satellite`), long press of `D` and PF
  action 8.
- **Icon**: drawn in code (like all OEM icons), 16×12, at x=150 in the status
  bar. Four states: hidden, grey, cyan and blinking green.

---

## Communication with the PC

### D-22 · CPS protocol extension with new commands (I, K, L)

**Decision.** R/W/E (24-bit addresses) from `cps.c` are reused and three
commands are added:

- `I` (info): detects firmware with satellite support.
- `K` (set UTC time).
- `L` (reload the database).

The tool **writes nothing** if `I` does not answer, so there is no risk with
a radio running the OEM firmware.

**Along the way** an overflow in `cps.c` was fixed: the payload buffer was
128 bytes and a full write carries 3 address bytes plus 128 data bytes.

### D-23 · Input event dispatcher

**Context.** In the base firmware, keys were queued as events but nobody
consumed them.

**Decision.** `task_events()` in `main.c` delivers events to
`radio_handle_key()` and `radio_handle_encoder()`. Key repeats are posted as
`EVT_KEY_LONG_PRESS` to tell long presses apart.

---

## Stock firmware (way A)

### D-24 · Five Doppler channels per satellite, computed from orbital height

**Decision.** The maximum Doppler at the horizon is computed from the
orbital velocity and the Earth's radius. Five symmetric steps (AOS, A2, TCA,
L2, LOS) are spread on a 0.5 kHz grid for RX. The uplink is corrected in the
opposite direction and in proportion to frequency. SO-50 gets an extra `ARM`
channel.

The report says when to change channel on each pass, with the channel
closest to the real Doppler.

**Format.** CHIRP CSV with `Duplex=split` and `Offset` equal to the TX
frequency. Satellites without a valid TX use `Duplex=off`. Memories start at
900 by default.

### D-25 · TLE sources and freshness rules

**Decision.** Sources are Celestrak (amateur and stations groups) and AMSAT
`nasabare`, merged keeping the most recent epoch per NORAD number. Each line
checksum is verified and *alpha-5* catalogue numbers are accepted.

- **Older than 14 days**: warning.
- **Older than 45 days**: rejected. For example, the last SO-125 (HADES-ICM)
  TLE is from May 2026, with an *ndot* of 0.065, typical of a re-entered
  satellite.

### D-26 · Editable JSON frequency database

**Decision.** `pc/rt950_toolkit/data/sat_freqs.json`, with data from AMSAT
(*Live FM Satellites*), AMSAT-EA and Orbital Space. The ISS has two entries
(voice and APRS), because each record is one transponder, as in OpenGD77.

---

## Quality

### D-27 · Host tests with the real code

**Decision.** The same firmware `.c` files are compiled on the PC:

- **SGP4 and geometry**: compared with `python-sgp4` and Skyfield.
- **Predictor**: compared with Skyfield's `find_events`.
- **End-to-end test**: the PC tool talks over a pseudo terminal to the real
  `cps.c`, which writes a simulated flash. The engine boots from that flash
  and the interface is drawn into a frame buffer saved as PNG.

**Consequences.** Everything that does not depend on the RF hardware is
verified. What does depend on it is in the [TESTING.en.md](TESTING.en.md)
plan.

### D-28 · GPL-3.0 licence

**Decision.** The same as the base firmware, so it can be merged upstream.
The parts derived from fdlibm keep their permission notice.

---

## RT-950 Toolkit (PC program)

### D-29 · Own cross-platform program reproducing the Editor's functions

**Context.** An application for Windows and Linux was requested that reads
and writes the codeplug, updates the satellites and has the functions of
RT-950/950Pro Editor. The installed Editor is a .NET application with a
WinUI interface: it only runs on Windows and its repository publishes
binaries, not code.

**Decision.** Write **RT-950 Toolkit** in Python with Tkinter and pyserial
(`pc/rt950_toolkit/`), with a graphical interface and a command line, and
integrate the satellite tool (`sat.py`) into it. It ships as a single
executable per system, built with PyInstaller.

**Alternatives.**

- Modifying the Editor: no source code, and it would not run on Linux.
- Qt (PySide): much larger executables and more dependencies.
- Web Serial in the browser: Chromium browsers only, and awkward access to
  local files.

**Consequences.** One program for both firmwares and both systems, about
20 MB, that can be tested without a radio (D-38). Tkinter looks plain, but it
comes with Python and needs nothing else installed.

### D-30 · Cross-check the protocol and encodings against the Editor

**Context.** The open firmware's reverse-engineering notes said tones were a
261-entry index. Writing with a wrong encoding would ruin every channel. The
Editor (MIT licence) is proven on real radios.

**Decision.** Use the installed Editor as a reference: its IL was listed
(metadata and instructions, with an own dumper based on
`System.Reflection.Metadata`) and the handshake, fixed challenge, key, block
plan, APRS page (`T`/`X`), tone, text and DTMF encodings and the `.dat`
format were checked one by one. No code was copied: the implementation is
original and the Editor is credited in the documentation.

**Consequences.** The tone encoding was corrected (CTCSS = frequency × 10,
little-endian; DCS = index + 1, +105 when inverted), as were the text
padding (`FF`), the transfer plan and the APRS page access. Everything is
described in [PROTOCOL.en.md](../toolkit/PROTOCOL.en.md).

### D-31 · Write only the edited fields

**Context.** Some bits have unknown meaning (FHSS learn, FHSS code, reserved
bytes) and firmware versions may use bytes the program does not know.

**Decision.** The codeplug keeps the memory regions as read. Each field only
changes its own bits (`_bits_set`, `encode_channel` with the previous
record), and everything else is written back unchanged.

**Consequences.** Reading and writing without changes leaves the radio
exactly as it was (checked with the emulator). A new channel starts with
zeros and with `FF` in the FHSS code and the name.

### D-32 · Mandatory backup and verify after writing

**Decision.** Before every write the radio is read and a dated backup is
saved. Afterwards each region is read and compared with what was written; any
mismatch is reported with its address. **Radio → Restore a backup** returns
to the previous state.

**Consequences.** Writing takes twice as long, but a cable or protocol
failure does not leave the radio in an unknown state.

### D-33 · Own `.rt950` format and CPS `.dat` from a template

**Context.** The CPS `.dat` is a .NET object serialized with BinaryFormatter
(MS-NRBF). The `netfleece` library does not work with Python 3.13 and there
is no specification of the CPS types.

**Decision.**

- Own `.rt950` format: JSON with the regions in base64 and metadata.
  Lossless and easy to inspect.
- `.dat`: own MS-NRBF parser (`nrbf.py`), which rewrites the file byte for
  byte, and `datfile.py`, which maps the CPS lists. Saving starts from an
  existing `.dat` and only changes its values.

**Consequences.** Files can be exchanged with the CPS and the Editor without
losing unknown fields. Saving in that format needs a `.dat` as template (the
one opened or one chosen).

### D-34 · VFO options read-only

**Context.** For bytes 26 to 28 of each VFO, the maker's model file and the
Editor disagree.

**Decision.** VFO frequency and offset, on which both sources agree, are
editable. The other VFO options are shown read-only with a notice.

**Consequences.** No risk of writing a value into the wrong bit. Those
options are changed on the radio until the map is confirmed on hardware.

### D-35 · Settings generated from the model file

**Decision.** The settings lists (131 select options, 58 text fields, 64
numeric values and 16 DTMF codes) are generated from `rt950pro_schema.json`,
the model document downloaded by the maker's app, with address, bit
positions and option names.

**Alternatives.** Hand-written lists: more mistakes and more work to follow
firmware versions.

**Consequences.** A fix to the model file applies everywhere. Its known
errata were corrected (an out-of-range address, an SSB offset without the
signed flag).

### D-36 · Automatic firmware detection and sector writes on the custom firmware

**Decision.** `protocol.detect()` sends `PROGRAMBT9000U`: a `06` answer means
stock firmware; an `A5` frame means custom. With the custom firmware, regions
are written by reading, modifying, erasing and rewriting each 4 KB sector.

**Consequences.** The user does not choose the protocol. With the custom
firmware, neighbouring flash data (satellite database, calibration) is not
erased.

### D-37 · Spanish and English interface, GBK text

**Decision.** All interface strings live in `i18n.py`, Spanish by default,
switchable in **Help → Language**. Radio names are encoded in GBK (code page
936), like the CPS, without splitting two-byte characters.

### D-38 · Distribution with GitHub Actions and tests with an emulator

**Decision.** `.github/workflows/build.yml` builds the firmware, runs the
host tests and builds `RT950Toolkit.exe` (Windows) and `RT950Toolkit`
(Linux) with PyInstaller. On every `v*` tag it publishes them in Releases.
`pc/tests/oem_emulator.py` emulates the programming port of a stock-firmware
radio on a pseudo terminal, and the tests run full read, write and verify
sessions against it.

**Consequences.** Every version is tested automatically. The Windows
executable is built on Windows, with no cross-compilation.

---

## APRS messaging

### D-39 · Messaging only in the custom firmware

**Context.** Radtel's stock firmware cannot be modified and only sends the
beacon with a fixed message.

**Decision.** Implement messaging in the custom firmware, on the BK4829 AFSK
chain already used by the beacon. RT-950 Toolkit still edits the beacon's
fixed message for the stock firmware.

### D-40 · Three layers: codec, engine and user interface

**Decision.**

- `aprs_msg_codec.c`: pure functions (APRS 1.0.1 formatting and parsing,
  AX.25 UI frame and FCS). No hardware access.
- `aprs_msg.c`: inbox, message numbers, retries, acks and the link to
  `aprs.c`.
- `aprs_msg_ui.c`: screens.

**Consequences.** Codec and engine are tested on the PC with an independent
AX.25 decoder and with `aprslib`.

### D-41 · Retries, acks and AX.25 destination

**Decision.**

- Retries after 30, 60 and 120 s (exponential, 3 by default, 0 to 5) and a
  60 s wait after the last copy before marking it not acknowledged. That is
  the usual behaviour of APRS radios.
- Automatic ack with a random 1.5 to 3 s delay, so as not to transmit at the
  same time as the digipeater repeating the message.
- Duplicate filter (last 8 sender + number pairs): copies are acked again
  but stored only once.
- Destination `APZ950`, from the `APZxxx` range reserved for experimental
  software.

### D-42 · Inbox in RAM, settings in flash and multi-tap entry

**Decision.**

- 24-message inbox in RAM: enough for a session and no flash wear. Settings
  (receive, automatic ack and retries) are stored at 0x0C9000 with CRC-32.
- Phone-style multi-tap keypad with quick texts on the `B` key, and direct
  reply from the message.
- `MSG` indicator in the status bar, entry from the menu (APRS Set →
  Messages) and PF action 9.

---

## Release 0.3.0: branding, countries, firmware, Android and distribution

### D-43 · BricoHams branding across the project

**Decision.** The application becomes **BricoHams RT-950 Toolkit**; the
mobile app, **BricoHams RT-950 Programmer**. The BricoHams logo (original
SVG) appears in the window header, the icon, the installer, the web site,
the app and a strip on the custom firmware boot screen ("BricoHams · FW
x.y.z"). Brand colours: `#23272B` and `#F26B1D`.

**Consequences.** The settings and backups folder becomes `BricoHamsRT950`.
Credits to the source projects (Hertzz58, KK4OXN, bartasx) stay in the help
and the documentation.

### D-44 · HTML help shared by the application and the web site

**Decision.** One HTML manual per language (`pc/rt950_toolkit/help/`), which
the application opens in the browser with F1 and the web site publishes at
`/help/`. The disclaimer and backup guide texts live in `legal.py` and are
exported to the mobile app.

**Alternatives.** Help inside Tkinter (no links or tables) or online only
(does not work offline).

### D-45 · Country codeplugs built from public lists

**Context.** A configuration for Spain was requested with one zone per call
district (EA1…EA9) holding its airports, PMR446 and CB in the last zone, and
the same for the United Kingdom.

**Decision.**

- Spanish zones per URE district; UK zones per ETCC region (the regions the
  repeater list uses) plus a simplex zone.
- Repeaters: the official URE and ukrepeater.net lists; airports: OurAirports
  (public domain). The lists are kept in `tools/country_sources/` with their
  date and verified with a checksum; `tools/build_country_data.py` builds the
  JSON files.
- Air band, PMR446 and CB are loaded with transmit disabled.
- 8.33 kHz designators are converted to the real frequency.
- When a zone does not fit in 99 channels, the ones closest to the user's
  locator are kept.

**Alternatives.** RepeaterBook (automated access to its data needs
authorisation and was not reachable from the development environment), hand-made lists (they go stale) or one zone per province (it
does not fit in 10 zones).

**Consequences.** The data can be regenerated for every release. Quality
depends on the sources, which the app and the disclaimer point out.

### D-46 · A partial codeplug never writes blank settings

**Context.** A codeplug made from scratch (for example a country codeplug)
has its settings at `0xFF`. Writing it whole would ruin the radio's
configuration.

**Decision.** Every codeplug knows which regions are a complete image
(`valid_regions`: read from the radio or a `.dat`) and which bytes were
changed (`touched`). When writing, an incomplete region is only sent if
something in it changed, and then those bytes are merged into the copy just
read from the radio. Without that read, the write is refused. An empty
channel table is a complete image.

**Consequences.** A country codeplug writes channels and zone names and
leaves the other bytes of the zone block, the settings and APRS untouched
(tested with the emulator).

### D-47 · Firmware updates from the application and the browser

**Decision.** The bootloader protocol (`tools/firmware_upload.py`) is built
into the application (`flasher.py`, Firmware tab and `flash` command) and
into a Web Serial page (`site/flasher/`). Firmware is published with the
GitHub releases with its SHA-256 and the site serves it from the same origin,
because the browser cannot download from another domain without CORS. The
site also offers the original V0.27 to go back.

**Consequences.** Both implementations are tested against a bootloader
emulator. The installed firmware cannot be read, so the backup guide
explains that the backup is the original `.BTF`.

### D-48 · Android app and web app from one JavaScript code base

**Context.** An Android programmer with the same functions was requested,
modelled on the bartasx (BLE protocol, MIT), SP3ARK (Android app, binaries
only) and erkanz (patched OEM firmware, no licence) projects.

**Decision.** A web app (`mobile/www`) that runs in the browser (Web
Bluetooth and Web Serial) and is packaged as an APK with Capacitor and the
`@capacitor-community/bluetooth-le` plugin. The core (`core.js`) is a
translation of the Python code; field tables and country codeplugs are
exported from Python, and a test requires the result to be byte-for-byte
identical.

**Alternatives.** Native Kotlin app (all code duplicated and no web
version), Kivy/BeeWare with Python (very limited Bluetooth LE) or modifying
SP3ARK's app (no source code). Nothing from erkanz's project was used: it
has no licence.

**Consequences.** The same app works on Android, in desktop Chrome and as a
PWA. Writing is differential (only changed blocks) so it is fast over
Bluetooth. The APK cannot be built in the development environment (no
Android SDK) and is built by GitHub Actions.

### D-49 · Bluetooth: the same protocol as the cable

**Decision.** Bluetooth uses the same `OemLink` as the cable: the radio
exposes `0xFFE0/0xFFE1` with the same frames, and only a token written to
`0xFF31` first is needed. Writes go in 20-byte chunks (minimum MTU) with
10 ms between chunks, and an extra `0x06` before the model is tolerated.

### D-50 · Installers, packages and signing

**Decision.**

- Windows: PyInstaller one-folder build + Inno Setup installer without
  administrator rights; PyInstaller bootloader compiled from source;
  executable and installer signed with `signtool` and a timestamp.
- Linux: **pure Python** `.deb` and Arch packages (pure-Python sgp4 and
  pyserial bundled), with launcher, icon, `.desktop` file and udev rule; plus
  a portable executable.
- A self-signed BricoHams certificate to start with, with the pipeline ready
  for SignPath, Azure Trusted Signing or a commercial certificate; private
  keys are only stored as GitHub secrets.

**Consequences.** Self-signing protects integrity and shows the publisher on
computers that trust the certificate, but **does not remove the SmartScreen
warning** for everyone: only a certificate from a recognised authority does
(explained in the documentation).

### D-51 · Project web site on GitHub Pages

**Decision.** The same GitHub Actions run publishes the home page (with the
latest release downloads read from the GitHub API), the manual, the web
flasher, the web programmer and the firmware.

### D-52 · Mandatory disclaimer

**Decision.** The application, the app and the web flasher show the
disclaimer and require accepting it (versioned, in case it changes) before
writing to the radio or flashing. The Windows installer shows it before
installing.

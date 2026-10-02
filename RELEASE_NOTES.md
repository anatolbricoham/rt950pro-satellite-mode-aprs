# BricoHams RT-950 0.3.0

**Español** · [English below](#english)

Primera versión con la marca **BricoHams**: toolkit de escritorio, app
Android, firmware custom y flasheador web para la Radtel RT-950 / RT-950 Pro.

> **Antes de escribir o flashear la radio, haz copia de seguridad.** Lee la
> [guía de copias de seguridad y el aviso de responsabilidad](https://github.com/anatolbricoham/rt950pro-satellite-mode/blob/main/docs/BACKUP_AND_DISCLAIMER.md).
> Todo está probado en el PC con emuladores, **todavía no en una radio
> física**: los probadores son bienvenidos.

## Qué descargar

| Sistema | Fichero |
|---|---|
| Windows 10/11 | `BricoHams-RT950-Toolkit-Setup-0.3.0.exe` (instalador) o `…-windows-portable.exe` |
| Debian / Ubuntu | `bricohams-rt950-toolkit_0.3.0_all.deb` → `sudo apt install ./bricohams-rt950-toolkit_0.3.0_all.deb` |
| Arch Linux | `bricohams-rt950-toolkit-0.3.0-1-any.pkg.tar.zst` → `sudo pacman -U …` |
| Otras distribuciones Linux | `BricoHams-RT950-Toolkit-0.3.0-linux-x64` |
| Android 7+ | `BricoHams-RT950-Programmer-0.3.0.apk` |
| Radio (firmware custom) | `rt950-bricohams-0.3.0.BTF` (+ `.sha256`) |
| Codeplugs | `codeplug-ES-0.3.0.*` (España), `codeplug-GB-0.3.0.*` (Reino Unido) |

Flasheador web y programador web: <https://anatolbricoham.github.io/rt950pro-satellite-mode/>

**Windows:** los ejecutables van firmados con el certificado de BricoHams
(autofirmado). Si SmartScreen avisa: *Más información → Ejecutar de todas
formas*. Los probadores pueden confiar en el certificado con
`install-certificate.ps1` (incluido). Comprueba las descargas con
`SHA256SUMS`.

## Novedades

- **Codeplugs por país**: España con zonas EA1…EA9 (repetidores de
  radioaficionado y aeropuertos cercanos) y una zona final con PMR446 y banda
  ciudadana; Reino Unido con regiones del ETCC, símplex y PMR446/CB. Para
  radio nueva o ya configurada: sobrescribir o añadir.
- **Actualización de firmware** desde la aplicación (pestaña Firmware) y
  desde el navegador, con los firmware publicados aquí o un `.BTF` propio.
- **App Android** por Bluetooth con las mismas operaciones que la de
  escritorio.
- **Manual de ayuda** (F1), guía de copias de seguridad y aviso de
  responsabilidad.
- **Instaladores firmados** para Windows y paquetes para Debian, Ubuntu y Arch.
- Escritura segura: nunca se borran los ajustes al cargar un codeplug
  parcial.

Detalle completo: [CHANGELOG](https://github.com/anatolbricoham/rt950pro-satellite-mode/blob/main/CHANGELOG.md).

---

<a id="english"></a>

# BricoHams RT-950 0.3.0 (English)

First release under the **BricoHams** brand: desktop toolkit, Android app,
custom firmware and web flasher for the Radtel RT-950 / RT-950 Pro.

> **Back up before writing or flashing the radio.** Read the
> [backup guide and disclaimer](https://github.com/anatolbricoham/rt950pro-satellite-mode/blob/main/docs/BACKUP_AND_DISCLAIMER.en.md).
> Everything is tested on the PC with emulators, **not yet on a real
> radio**: testers are welcome, especially in the UK.

## What to download

| System | File |
|---|---|
| Windows 10/11 | `BricoHams-RT950-Toolkit-Setup-0.3.0.exe` (installer) or `…-windows-portable.exe` |
| Debian / Ubuntu | `bricohams-rt950-toolkit_0.3.0_all.deb` → `sudo apt install ./bricohams-rt950-toolkit_0.3.0_all.deb` |
| Arch Linux | `bricohams-rt950-toolkit-0.3.0-1-any.pkg.tar.zst` → `sudo pacman -U …` |
| Other Linux distributions | `BricoHams-RT950-Toolkit-0.3.0-linux-x64` |
| Android 7+ | `BricoHams-RT950-Programmer-0.3.0.apk` |
| Radio (custom firmware) | `rt950-bricohams-0.3.0.BTF` (+ `.sha256`) |
| Codeplugs | `codeplug-ES-0.3.0.*` (Spain), `codeplug-GB-0.3.0.*` (United Kingdom) |

Web flasher and web programmer: <https://anatolbricoham.github.io/rt950pro-satellite-mode/>

**Windows:** the executables are signed with the BricoHams certificate
(self-signed). If SmartScreen warns: *More info → Run anyway*. Testers can
trust the certificate with `install-certificate.ps1` (included). Check the
downloads against `SHA256SUMS`.

## What's new

- **Country codeplugs**: Spain with zones EA1…EA9 (amateur repeaters and
  nearby airports) and a last zone with PMR446 and CB; United Kingdom with
  the ETCC regions, simplex and PMR446/CB. For a new or an already
  configured radio: overwrite or append.
- **Firmware update** from the application (Firmware tab) and from the
  browser, with the firmware published here or your own `.BTF`.
- **Android app** over Bluetooth with the same operations as the desktop one.
- **Help manual** (F1), backup guide and disclaimer.
- **Signed installers** for Windows and packages for Debian, Ubuntu and Arch.
- Safe writes: loading a partial codeplug never blanks the settings.

Full details: [CHANGELOG](https://github.com/anatolbricoham/rt950pro-satellite-mode/blob/main/CHANGELOG.md).

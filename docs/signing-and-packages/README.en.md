# Installers, packages and signing

*[Versión en español](README.md)*

## What every release contains

GitHub Actions (`.github/workflows/build.yml`) builds everything when a `v*`
tag is pushed and attaches it to the release:

| File | System | Notes |
|---|---|---|
| `BricoHams-RT950-Toolkit-Setup-x.y.z.exe` | Windows 10/11 x64 | Inno Setup installer (Spanish/English), no administrator rights needed. Program and installer signed. |
| `BricoHams-RT950-Toolkit-x.y.z-windows-portable.exe` | Windows | Single executable, signed. |
| `bricohams-rt950-toolkit_x.y.z_all.deb` | Debian 11+, Ubuntu 22.04+ | Pure Python: depends on `python3` and `python3-tk`; includes a udev rule for the cable, icon and launcher. |
| `bricohams-rt950-toolkit-x.y.z-1-any.pkg.tar.zst` | Arch Linux | Built with `makepkg` in an Arch container (`pc/packaging/arch/PKGBUILD`). |
| `BricoHams-RT950-Toolkit-x.y.z-linux-x64` | Other distributions | Portable executable (PyInstaller). |
| `BricoHams-RT950-Programmer-x.y.z.apk` | Android 7+ | Bluetooth app, signed with the BricoHams key. |
| `rt950-bricohams-x.y.z.BTF/.bin/.hex` | Radio | Custom firmware (and `.sha256`). |
| `codeplug-ES/GB-x.y.z.*` | All | Country codeplugs: `.rt950`, CHIRP CSV, RT-950 Editor CSV, zones and report. |
| `BricoHams-CodeSigning.cer`, `install-certificate.ps1` | Windows | Public signing certificate and a script to trust it. |
| `SHA256SUMS` | All | SHA-256 sums of every file. |

The web site (GitHub Pages) is published in the same run: home page, manual,
web flasher, web programmer and firmware.

## Windows signing and SmartScreen

Windows warns for two different reasons:

1. **Digital signature** (Authenticode): proves who publishes the file and
   that it was not modified. BricoHams signs the program and the installer
   with `signtool` and a DigiCert timestamp.
2. **SmartScreen reputation**: Microsoft only stops warning when the
   certificate belongs to a recognised authority **and** the file or
   publisher has built up downloads.

The current certificate is **self-signed** (CN=BricoHams, valid until 2031,
SHA-1 `5F32E97D9BF2A4F80642F52CDEAFE494457B78D4`). With it:

- The signature protects against tampering and the file shows "BricoHams"
  under Properties → Digital Signatures.
- On computers that trust the certificate (`install-certificate.ps1`, for
  testers) Windows shows "BricoHams" as verified publisher.
- Elsewhere SmartScreen may still show "Windows protected your PC": **More
  info → Run anyway**.

Measures so that **Windows Defender (antivirus)** does not flag a false
positive: installable one-folder build (not a self-extracting single
executable), PyInstaller bootloader compiled from source for every release,
no UPX, version metadata and signature. If it is still flagged, it can be
submitted to Microsoft for analysis at
<https://www.microsoft.com/wdsi/filesubmission>.

No warning for anyone needs a certificate from a recognised authority. The
pipeline is ready; only the secret changes:

| Option | Cost | What to change |
|---|---|---|
| [SignPath Foundation](https://signpath.org/) | Free for open-source projects (on application) | Replace the "Sign executables" step with the SignPath action |
| [Azure Trusted Signing](https://learn.microsoft.com/azure/trusted-signing/) | About USD 10/month | `azure/trusted-signing-action` |
| Commercial OV/EV certificate | USD 200–600/year | Upload the `.pfx` as `WINDOWS_CERT_PFX` |

## GitHub secrets

| Secret | Contents |
|---|---|
| `WINDOWS_CERT_PFX` | The signing `.pfx`, base64 |
| `WINDOWS_CERT_PASSWORD` | Its password |
| `ANDROID_KEYSTORE` | The Android keystore (`.jks`), base64 |
| `ANDROID_KEYSTORE_PASSWORD` | Its password (alias `bricohams`) |

`publicar.bat` sets them with `gh secret set` from the `secrets` folder
delivered separately. **Private keys are never committed to the
repository**: keep them somewhere safe; if the Android key is lost, new APK
versions cannot be installed over the old ones. Without the secrets the
pipeline still builds, unsigned (debug APK).

## Building by hand

```bash
# Windows (PowerShell, in pc/)
python packaging/windows/make_version_info.py
pyinstaller --clean --noconfirm rt950_toolkit.spec
iscc /DAppVersion=0.3.0 packaging\windows\installer.iss

# Debian / Ubuntu
sh pc/packaging/linux/build_deb.sh dist

# Arch Linux (in pc/packaging/arch)
BH_LOCAL=1 makepkg -si

# Android (in mobile/)
npm ci && npx cap sync android && (cd android && ./gradlew assembleRelease)
```

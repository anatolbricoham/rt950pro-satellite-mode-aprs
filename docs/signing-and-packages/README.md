# Instaladores, paquetes y firma

*[English version](README.en.md)*

## Qué se publica en cada versión

GitHub Actions (`.github/workflows/build.yml`) compila todo al subir una
etiqueta `v*` y lo adjunta a la versión:

| Fichero | Sistema | Notas |
|---|---|---|
| `BricoHams-RT950-Toolkit-Setup-x.y.z.exe` | Windows 10/11 x64 | Instalador Inno Setup (español/inglés), sin permisos de administrador. Programa y instalador firmados. |
| `BricoHams-RT950-Toolkit-x.y.z-windows-portable.exe` | Windows | Un solo ejecutable, firmado. |
| `bricohams-rt950-toolkit_x.y.z_all.deb` | Debian 11+, Ubuntu 22.04+ | Python puro: depende de `python3` y `python3-tk`; incluye regla udev para el cable, icono y lanzador. |
| `bricohams-rt950-toolkit-x.y.z-1-any.pkg.tar.zst` | Arch Linux | Generado con `makepkg` en un contenedor Arch (`pc/packaging/arch/PKGBUILD`). |
| `BricoHams-RT950-Toolkit-x.y.z-linux-x64` | Otras distribuciones | Ejecutable portable (PyInstaller). |
| `BricoHams-RT950-Programmer-x.y.z.apk` | Android 7+ | App Bluetooth, firmada con la clave de BricoHams. |
| `rt950-bricohams-x.y.z.BTF/.bin/.hex` | Radio | Firmware custom (y `.sha256`). |
| `codeplug-ES/GB-x.y.z.*` | Todos | Codeplugs de país: `.rt950`, CSV de CHIRP, CSV de RT-950 Editor, zonas e informe. |
| `BricoHams-CodeSigning.cer`, `install-certificate.ps1` | Windows | Certificado público de firma y script para confiar en él. |
| `SHA256SUMS` | Todos | Sumas SHA-256 de todos los ficheros. |

La web (GitHub Pages) se publica en la misma ejecución: portada, manual,
flasheador web, programador web y firmware.

## Firma en Windows y SmartScreen

Windows decide si avisa por dos cosas distintas:

1. **Firma digital** (Authenticode): demuestra quién publica el fichero y que
   no se ha modificado. BricoHams firma el programa y el instalador con
   `signtool` y sello de tiempo de DigiCert.
2. **Reputación de SmartScreen**: Microsoft solo deja de avisar cuando el
   certificado pertenece a una autoridad reconocida **y** el fichero o el
   editor ha acumulado descargas.

El certificado actual es **autofirmado** (CN=BricoHams, válido hasta 2031,
SHA-1 `5F32E97D9BF2A4F80642F52CDEAFE494457B78D4`). Con él:

- La firma protege contra modificaciones y el fichero muestra «BricoHams»
  en Propiedades → Firmas digitales.
- En equipos que confían en el certificado (`install-certificate.ps1`, para
  los probadores) Windows muestra «BricoHams» como editor verificado.
- En el resto, SmartScreen puede seguir mostrando «Windows protegió su PC»:
  **Más información → Ejecutar de todas formas**.

Medidas para que **Windows Defender (antivirus)** no lo marque como falso
positivo: versión instalable en carpeta (no un único ejecutable que se
autodescomprime), cargador de PyInstaller compilado desde el código en cada
versión, sin UPX, metadatos de versión y firma. Si aun así lo marca, se
puede enviar a Microsoft para análisis en
<https://www.microsoft.com/wdsi/filesubmission>.

Para que no haya ningún aviso para nadie hace falta un certificado de una
autoridad reconocida. El flujo ya está preparado; solo cambia el secreto:

| Opción | Coste | Qué cambiar |
|---|---|---|
| [SignPath Foundation](https://signpath.org/) | Gratis para proyectos de código abierto (hay que solicitarlo) | Sustituir el paso «Sign executables» por la acción de SignPath |
| [Azure Trusted Signing](https://learn.microsoft.com/azure/trusted-signing/) | Unos 10 USD/mes | Acción `azure/trusted-signing-action` |
| Certificado OV/EV comercial | 200–600 USD/año | Subir el `.pfx` como `WINDOWS_CERT_PFX` |

## Secretos de GitHub

| Secreto | Contenido |
|---|---|
| `WINDOWS_CERT_PFX` | El `.pfx` de firma en base64 |
| `WINDOWS_CERT_PASSWORD` | Su contraseña |
| `ANDROID_KEYSTORE` | El almacén de claves Android (`.jks`) en base64 |
| `ANDROID_KEYSTORE_PASSWORD` | Su contraseña (alias `bricohams`) |

`publicar.bat` los configura con `gh secret set` a partir de la carpeta
`secrets` que se entregó aparte. **Las claves privadas nunca se suben al
repositorio**: guárdalas en un lugar seguro; si se pierde la clave Android,
las nuevas versiones del APK no podrán instalarse encima de las anteriores.
Sin los secretos, el flujo compila igualmente pero sin firmar (APK de
depuración).

## Compilar a mano

```bash
# Windows (PowerShell, en pc/)
python packaging/windows/make_version_info.py
pyinstaller --clean --noconfirm rt950_toolkit.spec
iscc /DAppVersion=0.3.0 packaging\windows\installer.iss

# Debian / Ubuntu
sh pc/packaging/linux/build_deb.sh dist

# Arch Linux (en pc/packaging/arch)
BH_LOCAL=1 makepkg -si

# Android (en mobile/)
npm ci && npx cap sync android && (cd android && ./gradlew assembleRelease)
```

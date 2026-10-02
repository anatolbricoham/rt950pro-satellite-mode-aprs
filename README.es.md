<p align="center"><img src="pc/rt950_toolkit/data/brand/bricohams_logo.svg" alt="BricoHams" width="420"></p>

# BricoHams RT-950 — Toolkit, app Android, firmware custom y flasheador web

*[English version](README.md)* · **Web:** <https://anatolbricoham.github.io/rt950pro-satellite-mode/>

Todo lo necesario para programar y ampliar la **Radtel RT-950 / RT-950 Pro**,
por **BricoHams** (radioafición · hazlo tú mismo):

| | |
|---|---|
| **BricoHams RT-950 Toolkit** (Windows, Debian/Ubuntu, Arch) | Editor de configuración por cable para firmware original y custom, `.dat` del CPS, CSV de CHIRP y de RT-950 Editor, **codeplugs por país** (España EA1–EA9, Reino Unido), **actualización de firmware**, satélites, ayuda HTML |
| **BricoHams RT-950 Programmer** (APK Android + web) | La misma programación por **Bluetooth** desde el móvil o el navegador |
| **Firmware custom** | Modo satélite (SGP4, split RX/TX con Doppler), mensajería APRS, pantalla de arranque BricoHams |
| **Flasheador web** | Actualización de firmware desde Chrome/Edge (Web Serial), incluida la vuelta al firmware original |

> **Obra derivada.** Parte del firmware abierto de
> [Hertzz58/Radtel-RT950-Pro-Firmware](https://github.com/Hertzz58/Radtel-RT950-Pro-Firmware)
> (GPL-3.0; se conserva todo su historial). El protocolo de programación se
> ha contrastado con RT-950/950Pro Editor (KK4OXN, MIT) y el de Bluetooth
> está documentado por rt950-ble (bartasx, MIT).
>
> **Estado:** verificado en el PC (código real del firmware, emuladores de
> la radio y del cargador, Bluetooth simulado), **todavía no en una radio
> física**. Lee antes [Copias de seguridad y aviso de responsabilidad](docs/BACKUP_AND_DISCLAIMER.md).

## Descargas

Cada [versión](https://github.com/anatolbricoham/rt950pro-satellite-mode/releases)
incluye el instalador y el ejecutable portable de Windows firmados, el
`.deb`, el paquete de Arch, un ejecutable portable de Linux, el APK de
Android, el firmware (`.BTF/.bin/.hex`), los codeplugs de España y Reino
Unido, el certificado de firma y `SHA256SUMS`, todo generado por GitHub
Actions.

## Documentación

| | Español | English |
|---|---|---|
| Copias de seguridad y aviso | [BACKUP_AND_DISCLAIMER.md](docs/BACKUP_AND_DISCLAIMER.md) | [BACKUP_AND_DISCLAIMER.en.md](docs/BACKUP_AND_DISCLAIMER.en.md) |
| Toolkit (escritorio) | [docs/toolkit/README.md](docs/toolkit/README.md) | [docs/toolkit/README.en.md](docs/toolkit/README.en.md) |
| Codeplugs por país | [docs/country-codeplugs/README.md](docs/country-codeplugs/README.md) | [docs/country-codeplugs/README.en.md](docs/country-codeplugs/README.en.md) |
| App Android / web | [docs/android/README.md](docs/android/README.md) | [docs/android/README.en.md](docs/android/README.en.md) |
| Actualizar el firmware | [docs/firmware-flashing/README.md](docs/firmware-flashing/README.md) | [docs/firmware-flashing/README.en.md](docs/firmware-flashing/README.en.md) |
| Instaladores, paquetes y firma | [docs/signing-and-packages/README.md](docs/signing-and-packages/README.md) | [docs/signing-and-packages/README.en.md](docs/signing-and-packages/README.en.md) |
| Modo satélite | [docs/satellite/README.md](docs/satellite/README.md) | [docs/satellite/README.en.md](docs/satellite/README.en.md) |
| Mensajería APRS | [docs/aprs-messaging/README.md](docs/aprs-messaging/README.md) | [docs/aprs-messaging/README.en.md](docs/aprs-messaging/README.en.md) |
| Protocolo de programación | [docs/toolkit/PROTOCOL.md](docs/toolkit/PROTOCOL.md) | [docs/toolkit/PROTOCOL.en.md](docs/toolkit/PROTOCOL.en.md) |
| Decisiones de diseño (D-01…D-52) | [DESIGN_DECISIONS.md](docs/satellite/DESIGN_DECISIONS.md) | [DESIGN_DECISIONS.en.md](docs/satellite/DESIGN_DECISIONS.en.md) |
| Pruebas | [TESTING.md](docs/satellite/TESTING.md) | [TESTING.en.md](docs/satellite/TESTING.en.md) |
| Manual de usuario (HTML, también en la app con F1) | [help/es](pc/rt950_toolkit/help/es/index.html) | [help/en](pc/rt950_toolkit/help/en/index.html) |

## Modo satélite

Seguimiento de satélites de aficionado con split corregido por Doppler, como
la pantalla de satélites de OpenGD77 y el modo satélite de AnyTone: SGP4 en
la radio, predicción del próximo pase, gráfico polar, VFO A = RX de bajada y
VFO B = TX de subida, tono de armado de SO-50, reloj UTC por GPS o PC e icono
de satélite en la barra de estado. RT-950 Toolkit (o `tools/rt950_sat.py`)
descarga los TLE, calcula los pases y lo sube todo a la radio, y también
genera canales Doppler para radios con el firmware **original**.

![Pantallas del modo satélite](docs/satellite/img/pantallas.png)

## Mensajería APRS

Mensajes de texto APRS 1.0.1 por la cadena AFSK del BK4829: bandeja de 24
mensajes, redacción con multipulsación y textos rápidos, acuse automático,
reintentos a los 30/60/120 s, filtro de duplicados, boletines e indicador
`MSG`. Menú APRS Set → Messages o acción PF 9.

![Pantallas de mensajería APRS](docs/aprs-messaging/img/screens.png)

## Codeplugs por país

Un clic crea una configuración lista para una radio nueva o se añade a la
que ya tienes (sobrescribir o añadir): **España** con una zona por distrito
(EA1…EA9) con repetidores de radioaficionado y aeropuertos cercanos (solo
RX), y una última zona con PMR446 y banda ciudadana; **Reino Unido** con las
regiones del ETCC, símplex y PMR446/CB. Ver
[docs/country-codeplugs](docs/country-codeplugs/README.md).

## BricoHams RT-950 Toolkit

Aplicación de escritorio y línea de comandos multiplataforma (`pc/`): leer,
escribir y verificar la radio (firmware original y custom), canales, zonas,
VFO, ajustes, DTMF, FM/AM/SSB, APRS, ficheros `.dat` del CPS, CSV de CHIRP y
de RT-950 Editor, canales predefinidos, copias de seguridad automáticas,
satélites, logo de arranque, codeplugs por país, actualización de
firmware y manual de ayuda (F1).

![RT-950 Toolkit](docs/toolkit/img/es-channels.png)

## Compilar y probar

```bash
make                  # firmware (arm-none-eabi-gcc 12 o posterior)
make btf              # .BTF cifrado para subirlo con la herramienta OEM
make test-host        # pruebas en el PC: satélites, mensajería APRS, Toolkit
python pc/run_toolkit.py   # BricoHams RT-950 Toolkit desde el código fuente
sh tests/run_tests.sh      # todas las pruebas (PC, web, Bluetooth simulado)
```

Subir el firmware a la radio, mapa de memoria, notas de hardware y
herramientas del proyecto original: ver el [README en inglés](README.md).

## Licencia

GPL-3.0 (ver [LICENSE](LICENSE)). RT-950/950Pro Editor (KK4OXN, MIT) se ha
usado como referencia para verificar el protocolo de programación; no se ha
copiado su código.

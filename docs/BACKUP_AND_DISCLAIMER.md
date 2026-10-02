# Copias de seguridad y aviso de responsabilidad

*[English version](BACKUP_AND_DISCLAIMER.en.md)*

Pequeño manual con consejos para hacer copia de seguridad del **firmware** y
de la **configuración** (codeplug) de la Radtel RT-950 / RT-950 Pro antes de
usar BricoHams RT-950 Toolkit, la app de Android, el flasheador web o el
firmware custom.

## 1. Copia de la configuración (codeplug)

| Dónde | Cómo |
|---|---|
| Windows / Linux (Toolkit) | **Radio → Leer de la radio**. Se guarda sola una copia con fecha en la carpeta de copias (**Radio → Abrir carpeta de copias**): `%APPDATA%\BricoHamsRT950\backups` en Windows, `~/.config/BricoHamsRT950/backups` en Linux. |
| Android (app) | Pestaña **Radio → Leer de la radio**. La copia queda en **Ficheros → Copias de seguridad**; con ⤓ la guardas o la compartes (Drive, correo…). |
| Navegador (web) | Igual que en Android; las copias se guardan en el navegador, descárgalas con ⤓. |

Consejos:

1. **Lee la radio antes de tocar nada** y guarda además tu propia copia:
   **Archivo → Guardar** (`.rt950`). Si usas el programa oficial de Radtel,
   guarda también un `.dat` (**Archivo → Guardar como .dat del CPS**).
2. Antes de cada escritura el programa vuelve a leer la radio y guarda otra
   copia (`before-write`). Si algo sale mal: **Radio → Restaurar una copia**
   (en Android: ↺ en la copia y después **Escribir en la radio**).
3. Guarda las copias fuera del ordenador o del móvil (USB, nube) de vez en
   cuando, sobre todo la primera lectura de una radio nueva.
4. Los ficheros `.rt950` son los mismos en Windows, Linux, Android y la web.

## 2. Copia del firmware

- **El firmware instalado no se puede leer desde la radio**: el cargador de
  arranque del fabricante no tiene ninguna orden de lectura. Ningún programa
  puede hacer una copia del firmware que lleva tu radio.
- La copia de seguridad del firmware es **el fichero `.BTF` oficial** de la
  versión que tienes:
  1. Mira la versión en el menú de la radio (información / versión) y
     apúntala.
  2. Guarda el `.BTF` oficial de esa versión: web de Radtel, o la carpeta
     `binary/` del repositorio (V0.15, V0.18, V0.21 y V0.27). El flasheador web
     ofrece también la V0.27 original.
- Para **volver al firmware original**: pestaña **Firmware → Desde fichero**
  → elige el `.BTF` oficial → **Flashear firmware** (o el flasheador web).
- Si un flasheo se interrumpe y la radio no arranca: apágala, **mantén
  pulsadas las dos teclas laterales inferiores mientras la enciendes** (modo
  cargador) y vuelve a flashear marcando «La radio ya está en modo cargador».

## 3. Calibración

La calibración de fábrica (potencias, sensibilidad, desviación) está en la
memoria de la radio. Ni el Toolkit, ni la app, ni el firmware custom la
modifican. No borres la memoria completa de la radio con otras herramientas.

## 4. Antes de cada operación

- Batería por encima del 50 %.
- Cable bien encajado (o Bluetooth cerca de la radio, sin otros programas
  conectados a ella).
- No desconectes ni apagues la radio mientras se escribe o se flashea.
- Haz las pruebas de transmisión del firmware custom con **carga artificial**.

---

## Aviso de responsabilidad

BricoHams RT-950 Toolkit, la app BricoHams RT-950 Programmer, el firmware
custom, el flasheador web y los codeplugs precargados se distribuyen **«tal
cual», sin garantía de ningún tipo**, bajo la licencia GPL-3.0. No son
productos de Radtel ni están aprobados por el fabricante.

- Programar la radio o cambiar su firmware puede dejarla inutilizable,
  borrar su configuración o su calibración y anular la garantía del
  fabricante. **Lo haces bajo tu propia responsabilidad.**
- El firmware custom está en fase experimental y no se ha probado en todas
  las radios.
- Los codeplugs por país se generan con datos públicos (URE, ukrepeater.net,
  OurAirports) que pueden estar incompletos o desactualizados. Compruébalos
  antes de usarlos.
- Las frecuencias aeronáuticas, de PMR446 y de banda ciudadana se cargan
  **solo para recepción**. Transmitir en ellas con este equipo no está
  permitido. Cada usuario es responsable de cumplir la normativa de su país
  y las condiciones de su licencia de radioaficionado.
- Los autores y BricoHams no se hacen responsables de daños en equipos,
  pérdida de datos, interferencias ni de cualquier otro perjuicio derivado
  del uso de este software.

El programa de escritorio, la app y el flasheador web muestran este aviso y
piden aceptarlo antes de escribir en la radio o actualizar su firmware.

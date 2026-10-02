"""legal.py - Disclaimer and backup advice shown by the application
(the same text is in docs/BACKUP_AND_DISCLAIMER*.md and on the web site)."""

DISCLAIMER_VERSION = 1

DISCLAIMER = {
    "es": """AVISO DE RESPONSABILIDAD

BricoHams RT-950 Toolkit, el firmware custom, el flasheador web y los
codeplugs precargados se distribuyen "TAL CUAL", sin garantía de ningún tipo,
bajo la licencia GPL-3.0. No son productos de Radtel ni están aprobados por
el fabricante.

• Programar la radio o cambiar su firmware puede dejarla inutilizable,
  borrar su configuración o su calibración y anular la garantía del
  fabricante. Lo haces bajo tu propia responsabilidad.
• El firmware custom está en fase experimental y no se ha probado en todas
  las radios. Haz las pruebas de transmisión siempre con carga artificial.
• Los codeplugs por país se generan con datos públicos (URE, ukrepeater.net,
  OurAirports) que pueden estar incompletos o desactualizados. Compruébalos
  antes de usarlos.
• Las frecuencias aeronáuticas, de PMR446 y de banda ciudadana se cargan
  solo para recepción. Transmitir en ellas con este equipo no está
  permitido. Cada usuario es responsable de cumplir la normativa de su país
  y las condiciones de su licencia de radioaficionado.
• Los autores y BricoHams no se hacen responsables de daños en equipos,
  pérdida de datos, interferencias ni de cualquier otro perjuicio derivado
  del uso de este software.

Antes de escribir en la radio o actualizar su firmware, haz copia de
seguridad (menú Ayuda → Copias de seguridad).""",
    "en": """DISCLAIMER

BricoHams RT-950 Toolkit, the custom firmware, the web flasher and the
preloaded codeplugs are provided "AS IS", without warranty of any kind,
under the GPL-3.0 licence. They are not Radtel products and are not
endorsed by the manufacturer.

• Programming the radio or changing its firmware can make it unusable, erase
  its configuration or calibration and void the manufacturer's warranty.
  You do it at your own risk.
• The custom firmware is experimental and has not been tested on every
  radio. Always test transmission into a dummy load.
• Country codeplugs are built from public data (URE, ukrepeater.net,
  OurAirports) that may be incomplete or out of date. Check them before use.
• Air band, PMR446 and citizens' band frequencies are loaded for reception
  only. Transmitting on them with this radio is not allowed. Every user is
  responsible for complying with the regulations of their country and the
  terms of their amateur licence.
• The authors and BricoHams accept no liability for damaged equipment, lost
  data, interference or any other harm arising from the use of this
  software.

Before writing to the radio or updating its firmware, make a backup
(Help menu → Backups).""",
}

BACKUP_GUIDE = {
    "es": """COPIAS DE SEGURIDAD — CONSEJOS

1. Configuración (codeplug)
   • Radio → Leer de la radio. El programa guarda automáticamente una copia
     con fecha en la carpeta de copias (Radio → Abrir carpeta de copias).
   • Guarda además tu propia copia: Archivo → Guardar (.rt950) y, si usas
     el programa oficial, Archivo → Guardar como .dat del CPS.
   • Antes de cada escritura el programa vuelve a leer la radio y guarda otra
     copia ("before-write"). Para volver atrás: Radio → Restaurar una copia.
   • Copia la carpeta de copias a un USB o a la nube de vez en cuando.

2. Firmware
   • El firmware instalado NO se puede leer desde la radio: el cargador de
     arranque no tiene orden de lectura. La copia de seguridad del firmware
     es el fichero .BTF original.
   • Antes de instalar el firmware custom, anota la versión que lleva tu
     radio (menú de la radio → información/versión) y guarda el .BTF
     oficial de esa versión (web de Radtel, o la carpeta binary/ del
     repositorio para las versiones V0.15–V0.27).
   • Para volver al firmware original: pestaña Firmware → Desde fichero →
     elige el .BTF oficial → Flashear.
   • Si la radio no arranca tras un fallo: apágala, mantén pulsadas las dos
     teclas laterales inferiores mientras la enciendes (modo cargador) y
     flashea marcando "La radio ya está en modo cargador".

3. Calibración
   • La calibración de fábrica está en la memoria de la radio y no la toca
     ni el programa ni el firmware custom. No borres la memoria completa de
     la radio con otras herramientas.

4. Antes de cada operación
   • Batería cargada (más del 50 %), cable bien conectado y no desconectes
     durante la escritura o el flasheo.""",
    "en": """BACKUPS — ADVICE

1. Configuration (codeplug)
   • Radio → Read from radio. The program automatically saves a dated
     backup in the backups folder (Radio → Open backups folder).
   • Also keep your own copy: File → Save (.rt950) and, if you use the
     official software, File → Save as CPS .dat.
   • Before every write the program reads the radio again and saves another
     backup ("before-write"). To go back: Radio → Restore a backup.
   • Copy the backups folder to a USB stick or the cloud now and then.

2. Firmware
   • The installed firmware CANNOT be read from the radio: the bootloader
     has no read command. The firmware backup is the original .BTF file.
   • Before installing the custom firmware, note the version your radio runs
     (radio menu → information/version) and keep the official .BTF of that
     version (Radtel's web site, or the repository's binary/ folder for
     versions V0.15–V0.27).
   • To go back to the original firmware: Firmware tab → From file →
     choose the official .BTF → Flash.
   • If the radio does not start after a failure: switch it off, hold the two
     lower side keys while switching it on (bootloader mode) and flash with
     "Radio already in bootloader mode" ticked.

3. Calibration
   • Factory calibration lives in the radio's memory and is not touched by
     the program or the custom firmware. Do not erase the radio's whole
     memory with other tools.

4. Before every operation
   • Battery charged (above 50 %), cable firmly connected, and do not
     disconnect while writing or flashing.""",
}

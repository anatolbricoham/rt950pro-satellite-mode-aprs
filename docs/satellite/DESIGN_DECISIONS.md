# Decisiones de diseño — BricoHams RT-950: satélites, Toolkit, APRS, países, firmware y Android

*[English version](DESIGN_DECISIONS.en.md)*

Cada decisión sigue el formato **Contexto → Decisión → Alternativas →
Consecuencias**. Están numeradas (D-xx) para poder citarlas desde el código,
los issues y los commits.

---

## Alcance y arquitectura

### D-01 · Dos vías: firmware original y firmware custom

**Contexto.** El firmware abierto del RT-950 Pro está en fase alfa: LCD,
teclado y audio verificados, y BK4829, GPS y flash sin probar en hardware.
La gran mayoría de usuarios tienen el firmware de Radtel, que no se puede
modificar.

**Decisión.** Hay dos vías que comparten herramienta y base de frecuencias:

- **A**: canales Doppler en split para el firmware original.
- **B**: seguimiento real con SGP4 en el firmware custom.

**Alternativas.** Solo el firmware custom (inútil hoy para casi todos) o
solo canales (no aporta seguimiento real ni icono).

**Consecuencias.** El trabajo sirve desde el primer día y la vía B puede
madurar a la vez que el firmware base.

### D-02 · Herramienta de PC propia en lugar de modificar RT-950/950Pro Editor

> Revisada en D-29: la herramienta de satélites forma parte ahora de RT-950
> Toolkit, que también reproduce las funciones del Editor.

**Contexto.** El repositorio del Editor (KK4OXN, MIT) solo publica binarios.
Su código fuente no está disponible. El Editor sí importa CSV compatibles con
CHIRP.

**Decisión.** Crear `tools/rt950_sat.py`, que genera un CSV CHIRP para el
Editor (vía A) y habla directamente con el firmware custom (vía B). El
formato `satdb.bin` y el protocolo están documentados en
[PROTOCOL_AND_FORMAT.md](PROTOCOL_AND_FORMAT.md) para que el autor del Editor
pueda integrarlos.

**Alternativas.** Modificar el Editor: sin código fuente no se puede
mantener (ver D-30 sobre cómo sí se ha usado como referencia).

**Consecuencias.** No depende de terceros. La integración en el Editor se
puede proponer como *feature request* con esta especificación.

### D-03 · Cálculo orbital en la radio y, además, tabla de pases del PC

**Contexto.** El RT-950 Pro tiene GPS. OpenGD77 calcula las órbitas en la
propia radio. Una tabla de pases precalculada caduca y solo vale para un
QTH.

**Decisión.** La radio propaga con SGP4 a partir de los TLE: posición,
Doppler y predicción. El PC añade una tabla de pases como atajo, que se usa
cuando el QTH actual está a menos de 25 km del QTH del fichero y la máscara
coincide.

**Alternativas.** Solo tabla del PC (falla si viajas, y el Doppler en tiempo
real necesita la órbita igualmente) o solo cálculo en la radio (más CPU al
arrancar).

**Consecuencias.** Funciona en portable con GPS y arranca rápido en casa.
El PC y la radio usan el mismo algoritmo, así que los resultados coinciden al
segundo (verificado).

---

## Cálculo orbital

### D-04 · SGP4 *near-Earth* solamente (sin SDP4)

**Contexto.** Todos los satélites FM y los lineales en V/U son LEO (periodo
de 90 a 115 minutos). SDP4 añade unos 6 KB de código y mucha complejidad.

**Decisión.** Portar solo la rama near-Earth de Vallado (*Revisiting
Spacetrack Report #3*, modo "improved", WGS-72). Los objetos con periodo de
225 minutos o más se marcan `SAT_FLAG_DEEP_SPACE` y se rechazan con
`SGP4_ERR_DEEPSPACE`.

**Consecuencias.** Código pequeño y verificable. Un futuro satélite HEO
necesitaría añadir SDP4.

### D-05 · Doble precisión con biblioteca matemática propia

**Contexto.** El Cortex-M4F solo tiene FPU de simple precisión. SGP4 en
`float` acumula errores de varios km y de varias décimas de grado. El
firmware enlaza con `-nostdlib`, sin libm.

**Decisión.** Usar `double` (emulado por libgcc, que se enlaza con `-lgcc`) y
una biblioteca matemática propia, `sat_math.c`, con polinomios de fdlibm para
sin/cos/atan, reducción Cody-Waite y Newton para sqrt y cbrt.

**Alternativas.** Enlazar newlib libm (arrastra `errno`, `reent` y más
tamaño) o usar `float` (precisión insuficiente).

**Consecuencias.** Error relativo de 4·10⁻¹⁶ frente a libm. Una propagación
cuesta unos 1 ms en el AT32 (estimado), lo que obliga a la decisión D-08.
Ocupa unos 14 KB de flash.

### D-06 · WGS-72 para propagar y WGS-84 para el observador

**Decisión.** Las constantes de propagación son WGS-72, porque los TLE se
generan con ese modelo y usar otro introduce error. El observador va en el
elipsoide WGS-84, que es el que da el GPS.

### D-07 · TEME → ECEF solo con GMST (sin movimiento del polo ni dUT1)

**Contexto.** Para tener precisión de radioaficionado no hace falta la
cadena IERS completa.

**Decisión.** Rotar con GMST (IAU-82) y tratar UTC ≈ UT1.

**Consecuencias.** Error frente a Skyfield (con el modelo completo):
0,003° en elevación y 0,23 m/s en velocidad radial, es decir, **0,3 Hz a
437 MHz**. Es despreciable.

### D-08 · Predicción incremental con presupuesto por tick

**Contexto.** El scheduler es cooperativo y el IWDG reinicia la radio si una
tarea bloquea. Buscar un pase cuesta de 120 a 450 propagaciones (unas 190 de
media, medidas).

**Decisión.** `sat_pred_step()` avanza como máximo 12 propagaciones por
llamada. `sat_poll()` se ejecuta cada 100 ms, así que gasta como mucho un
12 % de CPU, y solo mientras quedan predicciones pendientes.

**Consecuencias.** La interfaz siempre responde. Con 48 satélites, la primera
predicción completa tarda unos 70 segundos, y solo si no se puede usar la
tabla del PC.

### D-09 · Algoritmo de búsqueda de pases

**Decisión.**

- Paso grueso según la elevación: 240 s por debajo de −40°, 120 s por
  debajo de −20°, 45 s por debajo de −8° y 20 s cerca del horizonte.
- Bisección a 1 s para AOS y LOS.
- Paso de 10 s y búsqueda ternaria para el TCA.
- Si al empezar el satélite ya está sobre el horizonte, se busca el AOS hacia
  atrás.

**Consecuencias.** Coincide al segundo con Skyfield en todos los pases del
banco de pruebas, incluido un roce de 0°. Se pueden perder roces de menos de
unos 20 s, que no se pueden trabajar.

---

## Datos y almacenamiento

### D-10 · Base de datos en la flash SPI, en 0x0C0000 (32 KB) y 0x0C8000 (config)

**Contexto.** La imagen de arranque OEM ocupa hasta 0x0B5800 y la primera
fuente OEM empieza en 0x15C000. El CPS OEM y el Editor usan direcciones de
16 bits (0x0000–0xFFFF).

**Decisión.** Reservar `SAT_DB` en 0x0C0000–0x0C7FFF y `SAT_CFG` en
0x0C8000–0x0C8FFF.

**Consecuencias.** Ni el CPS OEM ni el Editor pueden pisar estas zonas, y
volver al firmware OEM no las toca. Caben 48 satélites y 1024 pases. Un
`_Static_assert` garantiza que todo cabe.

### D-11 · Formato binario con registros fijos, TLE ya decodificados y CRC-32

**Decisión.** Cabecera de 64 bytes, registros de satélite de 128 bytes con
los elementos en `double` y registros de pase de 16 bytes. Todo en
little-endian y sin relleno. Lleva CRC-32 de cabecera y de carga útil, y
versión de formato.

**Alternativas.** TLE en texto (obliga a programar un analizador en la radio
y es más frágil) o formato de tamaño variable.

**Consecuencias.** La radio solo copia structs. Un `_Static_assert` en C y un
`assert` en Python mantienen el formato sincronizado.

### D-12 · Origen de la hora: GPS, después PC, después manual

**Decisión.** Reloj por software (base UTC + `get_tick()`).

- **GPS**: el RMC con fecha sincroniza en cuanto llega. Se añadió el
  análisis de la fecha en `gps.c`.
- **PC**: el comando CPS `K` al subir los datos.
- **Saltos**: si la hora cambia más de 60 s, se invalidan las predicciones.

**Consecuencias.** La deriva del oscilador es de unos segundos al día sin GPS.
Mientras no hay hora válida, la interfaz muestra `NO UTC TIME` y no
predice.

### D-13 · Posición en modo Auto: GPS o QTH del fichero

**Decisión.** Hay tres modos: Auto (GPS con fix y, si no, el QTH del fichero
o manual), solo GPS, y DB/manual. El QTH del fichero sale del locator que
indicaste en el PC (por defecto IM98IB). Solo se recalcula si te mueves más de
1 km, y las predicciones se invalidan si te mueves más de 10 km.

---

## Control de radio

### D-14 · VFO A = RX de bajada, VFO B = TX de subida; TX por defecto en el mismo chip

**Contexto.** Hay dos BK4829, pero comparten antena, relés de banda y la
conmutación T/R del PA. No está verificado que el RT-950 Pro pueda recibir en
un chip mientras el otro transmite.

**Decisión.** VFO A sigue la bajada y VFO B muestra la subida. Por defecto
(`TX VFO = Same(A)`), el PTT transmite la subida **en el chip del VFO A**
(split, como OpenGD77), que es seguro. Con `TX VFO = VFO B` se transmite con
el chip del VFO B y el A sigue en RX (estilo AnyTone). Esta opción es
experimental hasta que se verifique en hardware.

**Consecuencias.** El comportamiento por defecto es seguro y la arquitectura
ya está preparada para full-duplex si el hardware lo permite.

### D-15 · Doppler de RX cada segundo; TX fijo durante cada pasada

**Contexto.** `bk4829_set_frequency()` reescribe `REG_30 = 0xBFF1` (RX
enable). Llamarla en mitad de una transmisión la cortaría.

**Decisión.**

- **RX**: se corrige 1 vez por segundo, cuantizado a 100 Hz, y solo se
  retoca el chip si cambia el valor.
- **TX**: se calcula al pulsar PTT y se mantiene hasta soltarlo.

**Consecuencias.** En una pasada de 20 s, la subida a 435 MHz deriva como
mucho unos 2 kHz cerca del TCA, dentro de la tolerancia de un receptor FM
de satélite. En VHF la deriva es tres veces menor.

### D-16 · Nunca transmitir en satélites solo-RX o lineales

**Decisión.** `sat_ptt_prepare()` devuelve −1 si el satélite no tiene subida
FM válida, y `radio_ptt_on()` aborta. También se mantienen las comprobaciones
de BCL y de límites de banda calibrados, que liberan el estado del satélite
si fallan.

### D-17 · Tono de armado de un solo uso

**Decisión.** `*` arma la siguiente transmisión con `arm_tone` (74,4 Hz en
SO-50) y se desarma sola al soltar PTT. Es como lo usan los operadores: 2 s
con 74,4 Hz y después 67,0 Hz.

### D-18 · El modo satélite no deja huella

**Decisión.** Al entrar se guardan copias de VFO A y B y se pausa el dual
watch, que robaría el receptor. Al salir se restauran frecuencia, desplaza-
miento, tonos, DCS, ancho de banda y modulación.

**Consecuencias.** Verificado en el simulador: los VFO vuelven exactamente a
sus valores anteriores.

---

## Interfaz

### D-19 · Dibujo directo, refresco a 1 Hz y fuentes internas

**Contexto.** No hay frame buffer y el LCD se escribe por un bus de 8080
emulado por software.

**Decisión.** Redibujo completo solo al cambiar de pantalla. El resto se
refresca una vez por segundo, al pulsar una tecla o al empezar o terminar
una transmisión. Se usan las fuentes internas (5×7 y 8×8) en lugar de las
de la flash OEM, para no depender de ellas.

**Consecuencias.** Sin parpadeo apreciable y con poco uso del bus.

### D-20 · Textos en inglés en la radio

**Decisión.** Lo eligió el usuario. Es coherente con el resto del firmware y
con OpenGD77 (AOS, LOS, TCA, AZ, EL). La documentación está en español e
inglés, y RT-950 Toolkit tiene interfaz en los dos idiomas (D-37). El
informe de pases del PC está en español.

### D-21 · Accesos e icono

**Decisión.**

- **Accesos**: categoría 13 del menú (`Satellite`), pulsación larga de `D`
  y acción PF 8.
- **Icono**: se dibuja por código (como todos los iconos OEM), de 16×12, en
  x=150 de la barra de estado. Tiene cuatro estados: oculto, gris, cian y
  verde parpadeando.

---

## Comunicación con el PC

### D-22 · Extensión del protocolo CPS con comandos nuevos (I, K, L)

**Decisión.** Se reutilizan R/W/E (direcciones de 24 bits) de `cps.c` y se
añaden tres comandos:

- `I` (info): permite detectar el firmware con soporte de satélites.
- `K` (poner la hora UTC).
- `L` (recargar la base de datos).

La herramienta **no escribe nada** si `I` no responde, así que no hay riesgo
con una radio que lleve el firmware OEM.

**De paso** se corrigió un desbordamiento en `cps.c`: el búfer de carga
útil era de 128 bytes y una escritura completa trae 3 de dirección más 128 de
datos.

### D-23 · Despachador de eventos de entrada

**Contexto.** En el firmware base, las teclas se encolaban como eventos pero
nadie los consumía.

**Decisión.** `task_events()` en `main.c` lleva los eventos a
`radio_handle_key()` y `radio_handle_encoder()`. Las repeticiones de tecla se
publican como `EVT_KEY_LONG_PRESS` para distinguir la pulsación larga.

---

## Firmware original (vía A)

### D-24 · Cinco canales Doppler por satélite, calculados por altura orbital

**Decisión.** El Doppler máximo en el horizonte se calcula con la velocidad
orbital y el radio de la Tierra. Con eso se reparten 5 escalones simétricos
(AOS, A2, TCA, L2, LOS) en una rejilla de 0,5 kHz para RX. La subida se
corrige en sentido contrario y en proporción a la frecuencia. Para SO-50 se
añade un canal `ARM`.

El informe dice a qué hora cambiar de canal en cada pase, con el canal más
cercano al Doppler real.

**Formato.** CSV CHIRP con `Duplex=split` y `Offset` igual a la frecuencia de
TX. Los satélites sin TX válida usan `Duplex=off`. Las memorias empiezan en la
900 por defecto.

### D-25 · Fuentes de TLE y reglas de frescura

**Decisión.** Las fuentes son Celestrak (grupos amateur y stations) y AMSAT
`nasabare`, combinadas y quedándose con la época más reciente de cada NORAD.
Se comprueba el checksum de cada línea y se aceptan números de catálogo
*alpha-5*.

- **Más de 14 días**: se avisa.
- **Más de 45 días**: se descarta. Por ejemplo, el último TLE de SO-125
  (HADES-ICM) es de mayo de 2026, con un *ndot* de 0,065, típico de un
  satélite que ha reentrado.

### D-26 · Base de frecuencias en JSON editable

**Decisión.** `pc/rt950_toolkit/data/sat_freqs.json`, con datos de AMSAT (*Live FM
Satellites*), AMSAT-EA y Orbital Space. La ISS tiene dos entradas (voz y
APRS), porque cada registro es un transpondedor, como hace OpenGD77.

---

## Calidad

### D-27 · Pruebas en host con el código real

**Decisión.** Se compilan en el PC los mismos `.c` del firmware:

- **SGP4 y geometría**: se comparan con `python-sgp4` y Skyfield.
- **Predictor**: se compara con `find_events` de Skyfield.
- **Prueba de extremo a extremo**: la herramienta de PC habla por un
  pseudo-terminal con el `cps.c` real, que escribe una flash simulada. El
  motor arranca desde esa flash y la interfaz se dibuja en un frame buffer
  que se guarda como PNG.

**Consecuencias.** Todo lo que no depende del hardware RF queda verificado.
Lo que sí depende está en el plan de [TESTING.md](TESTING.md).

### D-28 · Licencia GPL-3.0

**Decisión.** La misma que el firmware base, para poder integrarlo aguas
arriba. Las partes derivadas de fdlibm conservan su aviso de permisos.

---

## RT-950 Toolkit (programa de PC)

### D-29 · Programa propio y multiplataforma que reproduce las funciones del Editor

**Contexto.** Se pidió una aplicación para Windows y Linux que lea y escriba
el codeplug, actualice los satélites y tenga las funciones de
RT-950/950Pro Editor. El Editor instalado es una aplicación .NET con
interfaz WinUI: solo funciona en Windows y su repositorio publica binarios,
no código.

**Decisión.** Escribir **RT-950 Toolkit** en Python con Tkinter y pyserial
(`pc/rt950_toolkit/`), con interfaz gráfica y línea de comandos, e integrar
en él la herramienta de satélites (`sat.py`). Se distribuye como un único
ejecutable por sistema, generado con PyInstaller.

**Alternativas.**

- Modificar el Editor: no hay código fuente y no funcionaría en Linux.
- Qt (PySide): ejecutables bastante más grandes y más dependencias.
- Web Serial en el navegador: solo navegadores Chromium y sin acceso a
  ficheros locales cómodo.

**Consecuencias.** Un solo programa para los dos firmwares y los dos sistemas,
de unos 20 MB, que se puede probar sin radio (D-38). Tkinter tiene un aspecto
sobrio, pero viene con Python y no necesita instalar nada más.

### D-30 · Contrastar el protocolo y las codificaciones con el Editor

**Contexto.** Las notas de ingeniería inversa del firmware abierto decían que
los tonos eran un índice de 261 entradas. Escribir con una codificación
equivocada estropearía todos los canales. El Editor (licencia MIT) está
probado con radios reales.

**Decisión.** Usar el Editor instalado como referencia: se listó su IL
(metadatos e instrucciones, con un volcador propio basado en
`System.Reflection.Metadata`) y se comprobaron uno por uno el saludo, el reto
fijo, la clave, el plan de bloques, la página APRS (`T`/`X`), las
codificaciones de tonos, textos y DTMF, y el formato `.dat`. No se ha copiado
código: la implementación es propia y el Editor se cita en la documentación.

**Consecuencias.** Se corrigió la codificación de tonos (CTCSS = frecuencia ×
10 en little-endian; DCS = índice + 1, +105 si es invertido), el relleno de
textos (`FF`), el plan de transferencia y el acceso a la página APRS. Todo
está descrito en [PROTOCOL.md](../toolkit/PROTOCOL.md).

### D-31 · Escribir solo los campos editados

**Contexto.** Hay bits cuyo significado no se conoce (aprendizaje FHSS,
código FHSS, bytes reservados) y versiones de firmware que pueden usar bytes
que el programa no conoce.

**Decisión.** El codeplug guarda las regiones de memoria tal como se leyeron.
Cada campo cambia solo sus bits (`_bits_set`, `encode_channel` con el
registro anterior), y lo demás se vuelve a escribir igual.

**Consecuencias.** Leer y escribir sin cambios deja la radio exactamente
igual (comprobado con el emulador). Un canal nuevo empieza con ceros y con
`FF` en el código FHSS y en el nombre.

### D-32 · Copia de seguridad obligatoria y verificación después de escribir

**Decisión.** Antes de cada escritura se lee la radio y se guarda una copia
con fecha. Después se lee cada región y se compara con lo escrito; si algo no
coincide se avisa con la dirección. **Radio → Restaurar una copia** vuelve al
estado anterior.

**Consecuencias.** La escritura tarda el doble, pero un fallo de cable o de
protocolo no deja la radio en un estado desconocido.

### D-33 · Formato `.rt950` propio y `.dat` del CPS a partir de una plantilla

**Contexto.** El `.dat` del CPS es un objeto .NET serializado con
BinaryFormatter (MS-NRBF). La biblioteca `netfleece` no funciona con Python
3.13 y BinaryFormatter no tiene especificación de los tipos del CPS.

**Decisión.**

- Formato propio `.rt950`: JSON con las regiones en base64 y metadatos. Sin
  pérdidas y fácil de inspeccionar.
- `.dat`: analizador MS-NRBF propio (`nrbf.py`), que reescribe el fichero
  byte a byte igual, y `datfile.py`, que traduce las listas del CPS. Para
  guardar se parte de un `.dat` existente y solo se cambian sus valores.

**Consecuencias.** Se pueden intercambiar ficheros con el CPS y con el Editor
sin perder los campos desconocidos. Hace falta un `.dat` como plantilla para
guardar en ese formato (el que se abrió o el que se elija).

### D-34 · Opciones del VFO en solo lectura

**Contexto.** En los bytes 26 a 28 de cada VFO, el fichero de modelo del
fabricante y el Editor no coinciden.

**Decisión.** Se editan la frecuencia y el desplazamiento de los VFO, que
coinciden en las dos fuentes. El resto de opciones del VFO se muestra en
solo lectura con un aviso.

**Consecuencias.** No hay riesgo de escribir un valor en el bit equivocado.
Esas opciones se cambian en la radio hasta que se confirme el mapa en
hardware.

### D-35 · Ajustes generados a partir del fichero de modelo

**Decisión.** Las listas de ajustes (131 opciones de selección, 58 textos, 64
valores numéricos y 16 códigos DTMF) se generan a partir de
`rt950pro_schema.json`, el documento de modelo que descarga la aplicación del
fabricante, con dirección, posición de bits y nombres de las opciones.

**Alternativas.** Escribir las listas a mano: más errores y más trabajo para
seguir las versiones del firmware.

**Consecuencias.** Una corrección del fichero de modelo se aplica a todo. Se
corrigieron sus erratas conocidas (una dirección fuera de rango, un
desplazamiento de SSB sin el indicador de signo).

### D-36 · Detección automática del firmware y escritura por sectores en el custom

**Decisión.** `protocol.detect()` envía `PROGRAMBT9000U`: si la respuesta es
`06`, es el firmware original; si es una trama `A5`, es el custom. Con el
custom, las regiones se escriben leyendo, modificando, borrando y
reescribiendo cada sector de 4 KB.

**Consecuencias.** El usuario no tiene que elegir el protocolo. Con el
firmware custom no se borran los datos vecinos de la flash (base de datos de
satélites, calibración).

### D-37 · Interfaz en español e inglés y textos en GBK

**Decisión.** Todos los textos de la interfaz están en `i18n.py`, con español
por defecto y cambio de idioma en **Ayuda → Idioma**. Los nombres de la
radio se codifican en GBK (página 936), como el CPS, sin cortar caracteres de
dos bytes.

### D-38 · Distribución con GitHub Actions y pruebas con un emulador

**Decisión.** `.github/workflows/build.yml` compila el firmware, ejecuta las
pruebas de host y genera con PyInstaller `RT950Toolkit.exe` (Windows) y
`RT950Toolkit` (Linux). En cada etiqueta `v*` los publica en Releases.
`pc/tests/oem_emulator.py` emula el puerto de programación de una radio con
firmware original sobre un pseudo-terminal, y las pruebas hacen sesiones
completas de lectura, escritura y verificación contra él.

**Consecuencias.** Cada versión se prueba de forma automática. El ejecutable
de Windows se genera en Windows sin necesidad de compilación cruzada.

---

## Mensajería APRS

### D-39 · Mensajería solo en el firmware custom

**Contexto.** El firmware original de Radtel no se puede modificar y solo
envía la baliza con un mensaje fijo.

**Decisión.** Implementar la mensajería en el firmware custom, sobre la
cadena AFSK del BK4829 que ya usa la baliza. RT-950 Toolkit sigue editando
el mensaje fijo de la baliza para el firmware original.

### D-40 · Tres capas: códec, motor e interfaz

**Decisión.**

- `aprs_msg_codec.c`: funciones puras (formato y análisis APRS 1.0.1, trama
  AX.25 UI y FCS). Sin acceso al hardware.
- `aprs_msg.c`: bandeja, números de mensaje, reintentos, acuses y conexión
  con `aprs.c`.
- `aprs_msg_ui.c`: pantallas.

**Consecuencias.** El códec y el motor se prueban en el PC con un
decodificador AX.25 independiente y con `aprslib`.

### D-41 · Reintentos, acuses y destino AX.25

**Decisión.**

- Reintentos a los 30, 60 y 120 s (exponencial, 3 por defecto, de 0 a 5) y
  60 s de espera tras la última copia antes de marcarlo como no confirmado.
  Es el comportamiento habitual de los equipos con APRS.
- Acuse automático con un retardo aleatorio de 1,5 a 3 s, para no transmitir
  a la vez que el digipetidor que repite el mensaje.
- Filtro de duplicados (últimos 8 remitente + número): las copias se
  confirman otra vez pero se guardan una sola vez.
- Destino `APZ950`, del rango `APZxxx` reservado para software experimental.

### D-42 · Bandeja en RAM, ajustes en flash e introducción por multipulsación

**Decisión.**

- Bandeja de 24 mensajes en RAM: suficiente para una sesión y sin desgastar
  la flash. Los ajustes (recepción, acuse automático y reintentos) se guardan
  en 0x0C9000 con CRC-32.
- Teclado de multipulsación como el de un teléfono, con textos rápidos en la
  tecla `B`, y respuesta directa desde el mensaje.
- Indicador `MSG` en la barra de estado, entrada desde el menú (APRS Set →
  Messages) y acción PF 9.

---

## Versión 0.3.0: marca, países, firmware, Android y distribución

### D-43 · Marca BricoHams en todo el proyecto

**Decisión.** La aplicación pasa a llamarse **BricoHams RT-950 Toolkit**; la
app móvil, **BricoHams RT-950 Programmer**. El logo de BricoHams (SVG
original) aparece en la cabecera de la ventana, el icono, el instalador, la
web, la app y una franja en la pantalla de arranque del firmware custom
(«BricoHams · FW x.y.z»). Colores corporativos: `#23272B` y `#F26B1D`.

**Consecuencias.** La carpeta de ajustes y copias pasa a ser `BricoHamsRT950`.
Los créditos de los proyectos de origen (Hertzz58, KK4OXN, bartasx) se
mantienen en la ayuda y en la documentación.

### D-44 · Ayuda en HTML compartida por la aplicación y la web

**Decisión.** Un único manual HTML por idioma (`pc/rt950_toolkit/help/`),
que la aplicación abre en el navegador con F1 y la web publica en `/help/`.
Los textos del aviso de responsabilidad y la guía de copias están en
`legal.py` y se exportan a la app móvil.

**Alternativas.** Ayuda dentro de Tkinter (sin enlaces ni tablas) o solo en
línea (no funciona sin conexión).

### D-45 · Codeplugs por país generados a partir de listas públicas

**Contexto.** Se pidió una configuración para España con una zona por
distrito (EA1…EA9) con los aeropuertos de cada una, PMR446 y CB en la última
zona, y lo mismo para el Reino Unido.

**Decisión.**

- Zonas de España por distrito URE; Reino Unido por región ETCC (las mismas
  regiones que usa la lista de repetidores) y una zona de símplex.
- Repetidores: lista oficial de la URE y de ukrepeater.net; aeropuertos:
  OurAirports (dominio público). Las listas se guardan en
  `tools/country_sources/` con su fecha y se comprueban con una suma de
  control; `tools/build_country_data.py` genera los JSON.
- Aeronáutica, PMR446 y CB se cargan con la transmisión desactivada.
- Los designadores de 8,33 kHz se convierten a la frecuencia real.
- Si una zona no cabe en 99 canales, se quedan los más cercanos al locator
  del usuario.

**Alternativas.** RepeaterBook (el acceso automático a sus datos requiere
autorización y no estaba accesible desde el entorno de desarrollo), listas a mano (se quedan viejas) o una zona por provincia
(no caben en 10 zonas).

**Consecuencias.** Los datos se pueden regenerar en cada versión. La calidad
depende de las fuentes, lo que se avisa en la app y en el aviso de
responsabilidad.

### D-46 · Un codeplug parcial nunca escribe ajustes vacíos

**Contexto.** Un codeplug creado desde cero (por ejemplo un codeplug de país)
tiene los ajustes a `0xFF`. Escribirlo entero estropearía la configuración de
la radio.

**Decisión.** Cada codeplug sabe qué regiones son una imagen completa
(`valid_regions`: leídas de la radio o de un `.dat`) y qué bytes se han
cambiado (`touched`). Al escribir, una región incompleta solo se envía si se
cambió algo en ella, y entonces se mezclan esos bytes con la copia que se
acaba de leer de la radio. Sin esa lectura, la escritura se rechaza. La
tabla de canales vacía sí es una imagen completa.

**Consecuencias.** Un codeplug de país escribe canales y nombres de zona y
deja intactos los demás bytes del bloque de zonas, los ajustes y el APRS
(probado con el emulador).

### D-47 · Actualización de firmware desde la aplicación y desde el navegador

**Decisión.** El protocolo del cargador (`tools/firmware_upload.py`) se
integra en la aplicación (`flasher.py`, pestaña Firmware y orden `flash`) y
en una página Web Serial (`site/flasher/`). Los firmware se publican en las
versiones de GitHub con su SHA-256 y la web los sirve desde el mismo sitio,
porque el navegador no puede descargar de otro dominio sin CORS. La web
ofrece también la V0.27 original para volver atrás.

**Consecuencias.** Las dos implementaciones se prueban contra un emulador
del cargador. El firmware instalado no se puede leer, así que la guía de
copias explica que la copia es el `.BTF` original.

### D-48 · App de Android y web con un único código JavaScript

**Contexto.** Se pidió un programador para Android con las mismas funciones,
tomando como modelo los proyectos de bartasx (protocolo BLE, MIT), SP3ARK
(app Android, solo binarios) y erkanz (firmware OEM parcheado, sin licencia).

**Decisión.** Una app web (`mobile/www`) que funciona en el navegador (Web
Bluetooth y Web Serial) y se empaqueta como APK con Capacitor y el plugin
`@capacitor-community/bluetooth-le`. El núcleo (`core.js`) es una traducción
del código Python; las tablas de campos y los codeplugs de país se exportan
desde Python, y una prueba exige que el resultado sea byte a byte igual.

**Alternativas.** App nativa en Kotlin (todo el código por duplicado y sin
versión web), Kivy/BeeWare con Python (Bluetooth LE muy limitado) o
modificar la app de SP3ARK (no hay código fuente). Del proyecto de erkanz no
se ha usado nada: no tiene licencia.

**Consecuencias.** La misma app sirve en Android, en Chrome de escritorio y
como PWA. La escritura es diferencial (solo bloques que cambian) para que
por Bluetooth sea rápida. El APK no se puede compilar en el entorno de
desarrollo (sin Android SDK) y se compila en GitHub Actions.

### D-49 · Bluetooth: mismo protocolo que el cable

**Decisión.** Por Bluetooth se usa el mismo `OemLink` que por cable: la radio
expone `0xFFE0/0xFFE1` con las mismas tramas, y solo hace falta escribir
antes un testigo en `0xFF31`. Se escribe en trozos de 20 bytes (MTU mínima),
con 10 ms entre trozos, y se tolera un `0x06` adicional antes del modelo.

### D-50 · Instaladores, paquetes y firma

**Decisión.**

- Windows: PyInstaller en modo carpeta + instalador Inno Setup sin
  administrador; cargador de PyInstaller compilado desde el código; ejecutable
  e instalador firmados con `signtool` y sello de tiempo.
- Linux: `.deb` y paquete de Arch de **Python puro** (sgp4 y pyserial
  incluidos en versión pura), con lanzador, icono, `.desktop` y regla udev;
  más un ejecutable portable.
- Certificado autofirmado de BricoHams para empezar, con el flujo preparado
  para SignPath, Azure Trusted Signing o un certificado comercial; las claves
  privadas solo se guardan como secretos de GitHub.

**Consecuencias.** La firma autofirmada protege la integridad y muestra el
editor en los equipos que confían en el certificado, pero **no elimina el
aviso de SmartScreen** para todos: eso solo lo consigue un certificado de una
autoridad reconocida (se explica en la documentación).

### D-51 · Web del proyecto en GitHub Pages

**Decisión.** La misma ejecución de GitHub Actions publica la portada (con
descargas de la última versión leídas de la API de GitHub), el manual, el
flasheador web, el programador web y el firmware.

### D-52 · Aviso de responsabilidad obligatorio

**Decisión.** La aplicación, la app y el flasheador web muestran el aviso y
exigen aceptarlo (con versión, por si cambia) antes de escribir en la radio o
flashear. El instalador de Windows lo muestra antes de instalar.

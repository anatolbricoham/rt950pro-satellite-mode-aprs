# Guía de uso — Modo Satélite RT-950 Pro

*[English version](USER_GUIDE.en.md)*

Todo lo que hace `rt950_sat.py` está también en la pestaña **Satélites** de
[RT-950 Toolkit](../toolkit/README.md), que además añade los canales Doppler
directamente al codeplug y escribe la radio.

## 1. Preparación en el PC

```bash
pip install sgp4 pyserial          # pillow/skyfield solo para las pruebas
cd tools
python rt950_sat.py all --locator IM98IB --hours 48 --min-el 0
```

| Opción | Por defecto | Significado |
|---|---|---|
| `--locator` | `IM98IB` | QTH en Maidenhead (4, 6 u 8 caracteres) |
| `--lat --lon --alt` | — | Coordenadas exactas (prevalecen sobre el locator) |
| `--hours` | 48 | Ventana de predicción del informe |
| `--min-el` | 0 | Máscara de horizonte en grados |
| `--tz` | `Europe/Madrid` | Zona horaria del informe |
| `--start-channel` | 900 | Primera memoria del CSV de canales |
| `--db-hours` | 72 | Horas de pases que se guardan en `satdb.bin` |
| `--tle-file` | — | Usar un fichero TLE local en lugar de descargarlo |
| `--offline` | — | No descargar; usar `rt950_sat_out/tle_cache.txt` |
| `--port` | — | Puerto serie: sube `satdb.bin` y pone la radio en hora |

Otros comandos:

```bash
python rt950_sat.py fetch                       # solo descargar TLE
python rt950_sat.py passes --min-el 10          # tabla en consola + informe
python rt950_sat.py chirp --start-channel 900   # solo el CSV de canales
python rt950_sat.py upload --port COM5          # subir un satdb.bin ya creado
python rt950_sat.py settime --port COM5         # solo poner la radio en hora UTC
python rt950_sat.py dump rt950_sat_out/satdb.bin
python ../pc/run_toolkit.py                     # interfaz gráfica (RT-950 Toolkit)
```

Para añadir o editar satélites, modifica `pc/rt950_toolkit/data/sat_freqs.json`. Cada
entrada indica el nombre que muestra la radio (12 caracteres como máximo), el
NORAD, el modo (`FM`, `FM_DATA`, `LIN_INV`, `LIN` o `RX_ONLY`), las
frecuencias en MHz, los tonos en Hz, las marcas (`scheduled`, `sunlit_only`)
y las opciones `enabled` y `favorite`. El aviso de AOS solo suena para los
favoritos y para el satélite que estés siguiendo.

## 2. Vía A — Firmware original

### Con RT-950 Toolkit

1. **Leer de la radio** (se guarda una copia de seguridad).
2. Pestaña **Satélites**: locator, **Actualizar TLE**, **Calcular pases**.
3. **Añadir canales Doppler desde el nº** (por defecto 900) y **Escribir en
   la radio**.
4. **Abrir informe** para ver a qué hora cambiar de canal.

### Con RT-950/950Pro Editor

1. Ejecuta `python rt950_sat.py all --locator TU_LOCATOR`.
2. En RT-950/950Pro Editor, lee la radio (**Radio > Read Radio**) y guarda
   una copia de seguridad.
3. Abre `rt950_sat_out/canales_satelite_chirp.csv` (el Editor abre CSV
   compatibles con CHIRP en una pestaña propia).
4. Copia los canales a la zona que prefieras (por defecto las memorias
   900–920) y escribe la radio (**Radio > Write Radio**) con la verificación
   activada.
5. Abre `pases_satelites.html`. En cada pase aparece **a qué hora** cambiar
   de canal, por ejemplo:

   `12:27:24 SO50 AOS · 12:31:14 SO50 A2 · 12:33:24 SO50 TCA · 12:34:44 SO50 L2 · 12:37:04 SO50 LOS`

Cada satélite FM ocupa 5 canales en split (RX en la bajada, TX en la subida
con su CTCSS), escalonados según la altura de su órbita. Para SO-50 hay
además un canal `SO50 ARM` con 74,4 Hz para armar el repetidor: mantén PTT
2 segundos en ese canal y pasa después a los canales Doppler.

## 3. Vía B — Firmware custom con modo satélite

### Cargar los datos

```bash
python rt950_sat.py all --locator IM98IB --port COM5
```

El programa comprueba que la radio tiene el firmware con soporte de satélites
(comando `I`) y no escribe nada si no lo encuentra. Después borra y escribe
los 32 KB reservados, los lee de vuelta para verificarlos, pone la radio en
hora UTC y le pide que recargue la base de datos. El icono de satélite
aparece en la barra de estado.

### Entrar en el modo satélite

Hay tres formas:

- **Menú → Satellite → Sat Mode → MENU**
- Pulsación larga de **D**
- Una tecla PF programada con la función 8 ("Satellite")

### Pantalla LISTA

Satélites ordenados por próximo AOS, con los que están a la vista arriba
(`LIVE`). Cada fila muestra la hora del AOS, el tiempo que falta, la
elevación máxima, los acimuts de entrada y salida y el modo.

| Tecla | Acción |
|---|---|
| Encoder, `*`/`B`, `0`/`A` | Mover el cursor |
| `MENU` | Seguir el satélite (pantalla SEGUIMIENTO) |
| `1` | Ver sus próximos pases |
| `D` | Ajustes del modo satélite |
| `#` | Salir del modo satélite (restaura los VFO) |

### Pantalla SEGUIMIENTO

- **Gráfico polar**: N arriba, círculos a 0°, 30° y 60° de elevación, la
  traza del pase (verde = AOS, rojo = LOS) y la posición actual (cuadro
  amarillo).
- **Columna derecha**: AZ, EL, distancia, velocidad radial (km/s; negativa
  = se acerca), altura, AOS/TCA/LOS, elevación máxima y acimuts.
- **RX (VFO A)**: bajada corregida, con el Doppler aplicado, el ajuste fino
  (`trim`) y el tono de squelch.
- **TX (VFO A o B)**: subida corregida, con su Doppler y su CTCSS. Se pone en
  rojo al transmitir. `RX ONLY` indica que la radio no transmitirá.

| Tecla | Acción |
|---|---|
| `PTT` | Transmitir en la subida corregida (el TX se calcula al pulsar) |
| Encoder | Ajuste fino de RX ±100 Hz (hasta ±20 kHz) |
| `5` | Poner el ajuste fino a cero |
| `*` | Armar: la **siguiente** transmisión usa el tono de armado (SO-50) |
| `0` | Activar o desactivar el Doppler |
| `A` / `B` | Satélite anterior o siguiente |
| `1` | Lista de pases de este satélite |
| `MENU` / `D` | Ajustes |
| `#` | Volver a la lista (deja de controlar los VFO) |

### Ajustes (menú → Satellite)

| Opción | Valores | Uso |
|---|---|---|
| Sat Mode | Enter | Entra en el modo satélite |
| Min Elev | 0–30° | Máscara de horizonte para las predicciones de la radio |
| Doppler | On/Off | Corrección Doppler de RX y TX |
| TX VFO | Same(A) / VFO B | Chip que transmite (ver la decisión D-14) |
| RX Bandw | Narrow/Wide | Ancho de banda de RX (Wide absorbe mejor el error Doppler) |
| Location | Auto / GPS / DB-Man | Origen del QTH |
| AOS Alert | Off, 1–15 min | Pitido antes del AOS (favoritos y satélite activo) |
| Sat Data | n sats | Número de satélites cargados (solo lectura) |

Si `Min Elev` coincide con la máscara usada en el PC y estás a menos de
25 km del QTH del fichero, la radio usa directamente la tabla de pases del
PC. En otro caso los calcula ella misma.

### Hora y posición

- **Hora**: se toma del GPS (RMC con fecha) en cuanto hay fix. Sin GPS, la
  pone el PC al subir los datos o con `settime`. La deriva del oscilador es de
  unos segundos al día, así que conviene resincronizar si llevas días sin GPS.
- **Posición**: en `Auto` se usa el GPS si tiene fix y, si no, el QTH del
  fichero (el locator que indicaste en el PC). La barra inferior muestra
  `QTH IM98ib (GPS|DB) UTC:GPS|PC`.

## 4. Ejemplo: SO-50

1. En la lista, SO-50 aparece con, por ejemplo, `10:27 in 3h21m · max 35deg
   187>041`.
2. Unos minutos antes del AOS suena el aviso y el icono parpadea. Pulsa
   `MENU` para entrar en el seguimiento.
3. Orienta la antena hacia el AOS (187°, sur). Con el satélite a la vista,
   la bajada se mueve sola, de +10 kHz a −10 kHz.
4. Pulsa `*` (`CTCSS 74.4 ARM!`) y mantén PTT 2 segundos para armar el
   temporizador de 10 minutos.
5. Después transmite con normalidad (CTCSS 67,0). Habla poco y escucha mucho.

## 5. Recomendaciones

- Usa una antena directiva (Arrow o Elk) o al menos una buena antena externa.
  Con la antena de goma solo se trabajan pases altos.
- Recuerda que el transpondedor de la ISS y el de PO-101 funcionan por
  horario. Consulta el estado en AMSAT.
- Respeta los planes de banda locales. Las subidas y bajadas de satélites en
  2 m y 70 cm son de uso exclusivo del servicio de aficionados por satélite.

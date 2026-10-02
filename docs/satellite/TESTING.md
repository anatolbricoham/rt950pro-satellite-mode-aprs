# Pruebas — Modo Satélite, mensajería APRS, RT-950 Toolkit, codeplugs por país, firmware y app Android

*[English version](TESTING.en.md)*

## Ejecutar

```bash
pip install sgp4 skyfield pyserial pillow aprslib
make            # firmware ARM: debe compilar con -Werror y sin avisos
make test-host  # = sh tests/run_tests.sh (pruebas 1 a 5)
python -m unittest discover -s pc/tests -v       # solo RT-950 Toolkit
xvfb-run python pc/tests/gui_smoke.py /tmp en     # interfaz gráfica (capturas)
node tests/web/flasher.test.cjs build/rt950-custom.BTF   # flasheador web
node tests/web/native_ble.test.cjs                 # Bluetooth de la app Android
python tests/web/app_e2e.py                        # app web/Android en Chromium (Playwright)
```

GitHub Actions ejecuta lo mismo en cada push (`.github/workflows/build.yml`).

## Qué se verifica en el PC (código real del firmware)

| # | Prueba | Referencia | Resultado |
|---|---|---|---|
| 1 | SGP4, posición y velocidad TEME (5 juegos de elementos, 11 instantes, de −1 a +7 días; incluye el caso de prueba del Spacetrack Report #3) | `python-sgp4` 2.27 | error ≤ 1,5·10⁻⁹ km y 2·10⁻¹² km/s |
| 1 | AZ/EL/distancia/velocidad radial (600 muestras aleatorias, ISS/SO-50/AO-123, QTH IM98IB) | Skyfield 1.55 | 0,002° de acimut, 0,003° de elevación, 36 m, 0,23 m/s (0,3 Hz a 437 MHz) |
| 1 | sin, cos, atan, asin, acos, sqrt, cbrt | libm | error relativo ≤ 4,4·10⁻¹⁶ |
| 2 | Predictor de pases (24 h, 19 pases, incluido un pase en curso y un roce de 0°) | `find_events` de Skyfield | AOS, TCA y LOS: 0 s de diferencia; elevación máxima ±0,5°; unas 186 propagaciones por pase |
| 3 | Subida por el protocolo CPS real (`cps.c`) sobre un pseudo-terminal | contenido de `satdb.bin` | flash idéntica, verificación OK, reloj = PC, recarga = 4 satélites |
| 3 | Próximo pase calculado en la "radio" frente al PC | `rt950_sat.py` | 0 s de diferencia |
| 3 | Seguimiento de SO-50 en pleno pase | cálculo directo | RX 436,8029 MHz (+7,9 kHz), TX 145,8473 MHz (−2,6 kHz), VFO A/B y chips iguales |
| 3 | PTT con armado | — | TX en el VFO A, índice de tono 3 (74,4 Hz) |
| 3 | Salida del modo satélite | estado previo | VFO A y B restaurados exactamente |
| 3 | Pantallas LIST / PASSES / TRACK / TX | inspección visual | PNG en `build_host/e2e/` |
| 4 | Mensaje saliente: direcciones, ruta y FCS | decodificador AX.25 propio de la prueba y `aprslib` | `EA7ABC-7>APZ950,WIDE1-1::EB5XYZ-7 :Hola desde el RT-950 Pro{601` |
| 4 | Acuse recibido / sin acuse | — | OK / 1 + 3 reintentos y después X |
| 4 | Mensaje entrante y su copia digipetida | — | 2 acuses, 1 entrada en la bandeja |
| 4 | Tráfico ajeno, boletín, texto con `{` | — | ignorado, guardado, rechazado |
| 4 | Respuesta con multipulsación y pantallas | inspección visual | enviada; PNG en `build_host/aprs/` |
| 5 | Tonos (261), BCD, canal completo, bits FHSS, nombres GBK | codificación inversa | idénticos |
| 5 | Todas las opciones de los 131 ajustes de selección, DTMF, valores con signo | — | se leen como se escriben |
| 5 | CSV nativo, CHIRP, del Editor (canales, zonas, FM/AM/SSB) | ida y vuelta + plantillas del Editor | idénticos; 16 canales PMR, 3 de plantilla, 10 zonas |
| 5 | `.dat` del CPS | `startup_default.dat` del Editor | MS-NRBF reescrito byte a byte igual; canal, zona e indicativo modificados y releídos |
| 5 | Lectura completa contra el emulador de radio | memoria del emulador | codeplug idéntico; APRS por `T` en la dirección 0; clave `RVB ` |
| 5 | Escritura + verificación, solo canales, modelo equivocado | memoria del emulador | idéntica; página APRS sin tocar; sesión rechazada |
| 5 | CLI `read` / `write --channels-only` (también con el ejecutable PyInstaller) | memoria del emulador | correcto, 2 copias de seguridad |
| 5 | Interfaz gráfica en inglés y español (Xvfb) | inspección visual | sin excepciones; capturas en `docs/toolkit/img/` |
| 6 | Codeplug de España: zonas EA1–EA9 + PMR-CB, 455 canales, PMR/CB/aeronáutica sin TX, LEMD en EA4, LEAB en EA5, desplazamientos −600 kHz / −7,6 MHz | datos de origen | correcto |
| 6 | Codeplug del Reino Unido: 10 zonas ≤ 99 canales, recorte por distancia (Heathrow se queda con IO91WM), PMR + CB 27/81 + CEPT | datos de origen | correcto |
| 6 | Modo añadir: conserva canales y nombres de zona, salta duplicados | — | correcto |
| 6 | Codeplug parcial sobre la radio emulada: solo canales y nombres de zona; ajustes, resto del bloque de zonas y APRS intactos; sin lectura previa se rechaza | memoria del emulador | correcto |
| 6 | Listas de origen copiadas (URE 144/432, ukrepeater.net) | suma de control calculada en la web de origen | idénticas |
| 7 | Flasheo desde el firmware y en modo cargador, modelo equivocado, fichero ajeno | emulador del cargador (Python y Node) | imagen recibida idéntica; rechazos correctos |
| 7 | Paquetes del flasheador Python y JavaScript | comparación byte a byte | idénticos |
| 8 | Núcleo JavaScript (app Android/web) frente a Python: decodificación, recodificación, codeplugs ES/GB y modo añadir | ficheros generados por Python | byte a byte idénticos |
| 8 | Protocolo JavaScript: lectura completa, escritura diferencial verificada, codeplug parcial, sin cambios = sin escrituras | radio emulada | correcto |
| 8 | Transporte Bluetooth nativo (Capacitor): testigo 0xFF31, hexadecimal, saludo y lectura | plugin simulado | correcto |
| 8 | Interfaz de la app en Chromium con Web Bluetooth simulado: aviso, conectar, leer, país (añadir), escribir | radio emulada | radio con EA1… y el canal propio conservado; ajustes intactos |
| 9 | `.deb` instalado y desinstalado; aplicación ejecutada solo con las dependencias incluidas | dpkg | correcto |
| 9 | Ejecutable PyInstaller (carpeta) | ejecución | correcto |

## Plan de pruebas en la radio (pendiente: requiere hardware)

Usa **siempre una carga artificial de 50 Ω** y, si es posible, un analizador
o un segundo receptor.

### Modo satélite

1. **Arranque**: el firmware arranca y, si no hay `satdb.bin`, el icono
   está oculto y la lista muestra "No satellite data".
2. **Subida**: `rt950_sat.py upload --port …` termina con "verify OK" y
   "N satellites". Aparece el icono gris.
3. **Hora**: sin GPS, la cabecera muestra la hora del PC (`UTC:PC`). Con GPS
   y fix, pasa a `UTC:GPS` y se mantiene a menos de 1 s.
4. **Posición**: con GPS, `QTH xxxxxx (GPS)`. Sin GPS, el locator del fichero
   con `(DB)`.
5. **Recepción**: durante un pase de la ISS o de SO-50, la frecuencia del
   VFO A cambia cada segundo y se oye el transpondedor. El encoder ajusta
   ±100 Hz.
6. **Transmisión (carga artificial)**: con PTT, el analizador muestra la
   frecuencia de subida corregida y el CTCSS correcto (67,0 Hz, o 74,4 Hz tras
   pulsar `*`). Al soltar, el RX vuelve a la bajada.
7. **TX VFO = VFO B**: comprobar que transmite el chip del VFO B y documentar
   si el VFO A sigue oyendo (full-duplex).
8. **Solo-RX**: con RS-44 seleccionado, el PTT no transmite.
9. **Salida**: `#` en la lista devuelve los VFO a su estado anterior.
10. **Robustez**: subir datos con el modo satélite abierto, quitar el GPS
    durante un pase y apagar y encender la radio (la configuración persiste).

### RT-950 Toolkit con una radio real

1. **Leer** una radio con firmware original y guardar el `.rt950`. Comprobar
   en la pestaña Canales que coinciden con lo que muestra la radio.
2. **Escribir sin cambios**: debe terminar con la verificación correcta y la
   radio debe quedar igual.
3. Cambiar un canal (nombre, tono CTCSS, DCS invertido, potencia) y una zona,
   escribir y comprobarlo en la radio.
4. Cambiar el indicativo APRS y comprobar que la escritura de la página APRS
   (`X`) termina bien (puede tardar varios segundos).
5. **Guardar como .dat** (con un `.dat` del CPS como plantilla) y comprobar
   que el fichero se abre en el CPS oficial y en el Editor.

### Mensajería APRS con una radio real

1. Programar indicativo, SSID y ruta. Sintonizar 144,800 MHz.
2. Enviar un mensaje a otra estación (o a un servicio como un iGate con
   respuesta automática) y comprobar en aprs.fi que la trama es correcta y
   que llega el `ack`.
3. Recibir un mensaje desde otra radio o desde aprs.fi y comprobar el acuse
   automático y el indicador `MSG`.

### Codeplug por país, firmware y Android con una radio real

1. Radio nueva (o tras copia): Codeplug por país → Sobrescribir → Escribir.
   Comprobar zonas EA1…, un repetidor cercano y la recepción de un aeropuerto
   en AM. Comprobar que PMR y CB no transmiten.
2. Radio configurada: Leer → Añadir → Escribir. Comprobar que los canales
   propios siguen y que los ajustes no han cambiado.
3. Flashear el firmware custom desde la aplicación y volver a la V0.27
   oficial desde el flasheador web.
4. App Android: conectar por Bluetooth, leer, editar un canal, escribir y
   comprobarlo en la radio.

Anota los resultados en un issue con la versión del firmware, la del
`satdb.bin` y las capturas.

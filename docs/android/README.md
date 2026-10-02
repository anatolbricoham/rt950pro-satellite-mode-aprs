# BricoHams RT-950 Programmer — Android y web

*[English version](README.en.md)*

Programador de la Radtel RT-950 / RT-950 Pro **por Bluetooth** desde el
móvil, con las mismas operaciones que la aplicación de Windows y Linux. El
mismo código funciona como **APK de Android** y como **página web** (Chrome
en Android, Windows, Linux o macOS).

| Radio | Canales | País | Ajustes |
|---|---|---|---|
| ![Radio](img/radio.png) | ![Canales](img/channels.png) | ![País](img/country.png) | ![Ajustes](img/settings.png) |

## Instalar

| Opción | Cómo |
|---|---|
| APK | Descarga `BricoHams-RT950-Programmer-x.y.z.apk` de la última versión, ábrelo en el móvil y permite «instalar aplicaciones desconocidas» para el navegador o el gestor de archivos. Android 7 o posterior con Bluetooth LE. |
| Web | Abre `https://anatolbricoham.github.io/rt950pro-satellite-mode/app/` en Chrome. En Android puedes usar «Añadir a pantalla de inicio». |

El APK está firmado con la clave de BricoHams (`es.bricohams.rt950`); las
actualizaciones se instalan encima mientras la firma sea la misma.

## Qué hace

| Función | Detalle |
|---|---|
| Conectar | **Bluetooth** (módulo BLE de la radio) o **cable USB** (Web Serial, en Chrome/Edge de escritorio). |
| Leer | Toda la configuración. Guarda una copia automática en **Ficheros → Copias de seguridad**. |
| Canales | Lista por zona, búsqueda por nombre, frecuencia, tono o modo; editor con RX/TX, tonos CTCSS/DCS, potencia, ancho, FM/AM, TX permitida, escaneo, bloqueo y PTT-ID; añadir y borrar. |
| Zonas | Nombres de las 10 zonas. |
| País | Codeplugs de España y Reino Unido, sobrescribir o añadir, locator, vista previa. |
| Ajustes | Todos los ajustes de selección y texto del fichero de modelo (generales, teclas, DTMF, FM/AM/SSB, APRS), con búsqueda. |
| Escribir | Vuelve a leer la radio (copia `before-write`), envía **solo los bloques que cambian** y verifica cada uno. Un codeplug creado desde cero solo escribe canales y nombres de zona. |
| Ficheros | Abrir y guardar `.rt950` (los mismos del Toolkit de escritorio), exportar CSV de CHIRP; en el APK se comparten con el menú de Android. |
| Ayuda | Guía, copias de seguridad, aviso de responsabilidad (que hay que aceptar antes de escribir). |

La actualización del **firmware** no se hace por Bluetooth: usa el Toolkit
de escritorio o el [flasheador web](../firmware-flashing/README.md) con el
cable.

## Uso

1. Activa el Bluetooth de la radio en su menú.
2. Pestaña **Radio → Bluetooth** y elige la radio (aparece como RT-950,
   Radtel o walkie-talkie). En Android acepta el permiso «Dispositivos
   cercanos».
3. **Leer de la radio**, edita o carga un codeplug de país y **Escribir en la
   radio**.

Por Bluetooth una lectura completa tarda más que por cable (unos 300 bloques
de 128 bytes); la escritura diferencial solo envía lo que cambia.

## Cómo funciona

- `mobile/www/js/core.js`: codificación de canales, tonos, ajustes, fichero
  `.rt950`, protocolo de programación, codeplugs de país y CSV. Es una
  traducción del código Python del Toolkit; las tablas de campos se exportan
  desde Python (`tools/export_web_meta.py`) y una prueba comprueba que el
  resultado es **byte a byte idéntico** al de Python.
- `mobile/www/js/transport.js`: Bluetooth por Web Bluetooth o por el plugin
  nativo `@capacitor-community/bluetooth-le` (APK), y cable por Web Serial.
- Protocolo Bluetooth: la radio expone el servicio `0xFFE0` con la
  característica `0xFFE1` (escritura + notificación), que transporta
  exactamente las mismas tramas que el cable. Antes del saludo hay que
  escribir un testigo de 20 bytes (`????` 0x02 + 15 aleatorios) en `0xFF31`.
  Datos documentados por el proyecto
  [rt950-ble](https://github.com/bartasx/Radtel-RT-950-PRO-bluetooth-reverse-engineering)
  (bartasx, MIT); la app de SP3ARK sirvió de referencia de funciones.
- `mobile/` es un proyecto [Capacitor](https://capacitorjs.com/) 8;
  GitHub Actions genera y firma el APK (`npm ci`, `npx cap sync android`,
  `./gradlew assembleRelease`).

## Compilar

```bash
cd mobile
npm ci
npx cap sync android
cd android && ./gradlew assembleDebug        # requiere Android SDK y JDK 21
```

Para firmar en release: variables `BH_KEYSTORE`, `BH_KEYSTORE_PASSWORD`,
`BH_KEY_ALIAS` (ver [firma y paquetes](../signing-and-packages/README.md)).

## Estado

El núcleo, el protocolo y la interfaz están probados con un emulador de la
radio (Bluetooth simulado en Chromium y plugin nativo simulado en Node). El
APK se compila en GitHub Actions; **todavía no se ha probado con una radio
real por Bluetooth**.

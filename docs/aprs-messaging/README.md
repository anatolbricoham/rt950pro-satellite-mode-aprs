# Mensajería APRS — RT-950 Pro (firmware custom)

*[English version](README.en.md)*

Mensajes de texto APRS en las dos direcciones, con acuse de recibo, como los
de un Kenwood TH-D74 o un Yaesu FTM-400: la radio recibe los mensajes
dirigidos a tu indicativo, los confirma sola, guarda una bandeja de entrada y
te deja escribir y enviar mensajes desde el teclado.

![Pantallas de mensajería](img/screens.png)

*Bandeja, mensaje recibido (boletín), redacción de una respuesta y bandeja
después de enviarla. Capturas del simulador de host con el código real del
firmware.*

## ¿Se puede con el firmware original?

No. El firmware de Radtel solo envía la baliza de posición con un **mensaje
fijo** (*Custom Messages*, 40 caracteres), que se configura con el CPS o con
RT-950 Toolkit. No tiene bandeja de entrada, ni redacción, ni acuses. La
mensajería solo existe en el firmware custom de este repositorio.

## Qué hace

- **Recibir**: el demodulador AFSK del BK4829 decodifica cada trama. Si es un
  mensaje para tu indicativo y SSID (o un boletín `BLNx`), se guarda en la
  bandeja y aparece `MSG` en la barra de estado.
- **Acuse automático**: a cada mensaje con número (`{id`) se responde con
  `ack<id>` tras un retardo aleatorio de 1,5 a 3 s, para no pisar al
  digipetidor. Las copias repetidas (digipetidas o reenviadas por el
  remitente) se vuelven a confirmar pero se guardan una sola vez.
- **Enviar**: cada mensaje lleva un número. Si no llega el `ack`, se repite a
  los 30, 60 y 120 s (3 reintentos por defecto, ajustable de 0 a 5). Si
  después de la última copia pasan 60 s sin acuse, se marca **X** (no
  confirmado). Con acuse se marca **OK**.
- **Formato**: APRS 1.0.1, capítulo 14:
  `:EA4BBB   :texto{12` (mensaje), `:EA4BBB   :ack12` (acuse),
  `:EA4BBB   :rej12` (rechazo), `:BLN1     :texto` (boletín). Destino AX.25
  `APZ950` (rango experimental `APZxxx`) y la ruta configurada en la radio
  (WIDE1-1, WIDE1-1,WIDE2-1 o personalizada).
- Hasta 67 caracteres por mensaje y 24 mensajes en la bandeja (en RAM: se
  pierden al apagar).

## Cómo se usa

### Entrar

- **Menú → APRS Set → Messages** (el valor muestra los no leídos), o
- una tecla PF programada con la acción 9.

### Bandeja

Mensajes más recientes arriba. `<` = recibido, `>` = enviado. A la derecha:
tiempo transcurrido y estado (`*` no leído, `..` esperando acuse, `OK`
confirmado, `X` sin confirmar).

| Tecla | Acción |
|---|---|
| Encoder | Mover |
| `MENU` | Abrir el mensaje |
| `*` | Mensaje nuevo |
| `#` | Salir |

### Mensaje

`MENU` responde al remitente; `#` vuelve.

### Redactar

1. **To**: indicativo del destinatario con multipulsación (2 = ABC2, 3 =
   DEF3, …, 0 = espacio y 0, 1 = `1.,-?!/@#*`). `*` borra. `MENU` pasa al
   texto.
2. **Text**: texto con multipulsación; `B` va rellenando textos rápidos
   (`QSL? 73`, `QRV`, `QRT, 73`, …). `MENU` envía. `#` vuelve.

### Ajustes (menú APRS Set)

| Opción | Valores | Por defecto |
|---|---|---|
| Messages | entrar | — |
| Msg RX | Off / On (mantener el demodulador activo) | On |
| Auto ACK | Off / On | On |
| Msg Retries | 0–5 | 3 |

Se guardan en la flash SPI en 0x0C9000, con CRC-32.

El indicativo, el SSID y la ruta son los de la configuración APRS de la
radio (los mismos que edita RT-950 Toolkit en la pestaña APRS).

## Requisitos y límites

- Configura tu indicativo y SSID antes de usarlo; sin indicativo no se envía
  nada.
- El canal tiene que ser el de APRS de tu región (144,800 MHz en Europa) y
  la radio tiene que estar escuchándolo para recibir.
- La transmisión usa la misma cadena AFSK que la baliza del firmware custom.
  **No se ha probado todavía en una radio física**: la codificación, el
  motor y la interfaz están verificados en el PC (ver abajo), pero la
  modulación del BK4829 no.

## Verificación

`tests/test_aprs_msg.py` (incluido en `tests/run_tests.sh`) compila el
códec, el motor y la interfaz reales en el simulador de host y comprueba:

- tramas salientes correctas, decodificadas por un decodificador AX.25
  independiente y por `aprslib`;
- acuse recibido → **OK**; mensaje sin respuesta → 1 + 3 reintentos y
  después **X**;
- acuse automático (también de la copia duplicada) y una sola entrada en la
  bandeja;
- tráfico para otras estaciones ignorado; boletines guardados; texto con `{`
  rechazado;
- respuesta escrita con multipulsación y enviada; pantallas guardadas como
  PNG.

Las decisiones de diseño están en
[DESIGN_DECISIONS.md](../satellite/DESIGN_DECISIONS.md) (D-39 a D-42).

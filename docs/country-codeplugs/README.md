# Codeplugs precargados por país

*[English version](README.en.md)*

BricoHams RT-950 Toolkit (Windows/Linux), la app de Android y el programador
web cargan una configuración lista para usar en las 10 zonas de la radio.
Sirven para una radio nueva o para una ya configurada:

- **Sobrescribir**: sustituye todos los canales.
- **Añadir**: conserva tus canales; los del país van a los huecos libres de
  cada zona y se saltan los que ya tienes (misma RX, TX y tono).

Un codeplug de país creado desde cero **solo escribe canales y nombres de
zona**: los ajustes de la radio, el APRS y la calibración no se tocan (ver
D-46).

## España

| Zona | Nombre | Contenido |
|---|---|---|
| 1–9 | EA1 … EA9 | Repetidores FM analógicos de 2 m y 70 cm del distrito (lista oficial de la URE, nombre `ED4YAD V` / `ED4YAN U`), y frecuencias de los aeropuertos y aeródromos del distrito: torre, aproximación, ATIS, rodadura e información (`LEMD TWR`, `LEMD ATIS`…), **solo RX, AM** |
| 10 | PMR-CB | PMR446 canales 1–16 (446,00625–446,19375 MHz, estrecho) y CB-27 canales 1–40 (26,965–27,405 MHz, AM), **solo RX** |

Distritos (URE / URVAG): EA1 Galicia, Asturias, Cantabria, Castilla y León y
La Rioja · EA2 País Vasco, Navarra y Aragón · EA3 Cataluña · EA4 Madrid,
Castilla-La Mancha (salvo Albacete) y Extremadura · EA5 Comunidad
Valenciana, Murcia y Albacete · EA6 Baleares · EA7 Andalucía · EA8 Canarias ·
EA9 Ceuta y Melilla.

Contenido de la versión 0.3.0: 455 canales (EA1 86, EA2 38, EA3 46, EA4 51,
EA5 51, EA6 24, EA7 57, EA8 42, EA9 4, PMR-CB 56). Todas las zonas caben en
99 canales.

## Reino Unido

| Zona | Nombre | Contenido |
|---|---|---|
| 1 | G SE LONDON | Región ETCC South East |
| 2 | G SW+CI | South West e islas del Canal (GJ, GU) |
| 3 | G MIDLANDS | Central |
| 4 | G EAST ANGLIA | East Anglia |
| 5 | G NORTH+GD | North e isla de Man (GD) |
| 6 | GW WALES | Wales & Marches |
| 7 | GM SCOTLAND | Scotland |
| 8 | GI N.IRELAND | Northern Ireland |
| 9 | UK SIMPLEX | Llamada y símplex de 2 m y 70 cm (IARU R1), APRS 144,800, ISS voz y APRS |
| 10 | PMR-CB | PMR446, CB UK 27/81 (27,60125–27,99125 MHz FM) y CB CEPT 1–40 (FM), **solo RX** |

Zonas 1–8: repetidores FM analógicos de 2 m y 70 cm operativos de la lista
de ukrepeater.net (RSGB ETCC; se excluyen los de solo modos digitales, los
fuera de servicio, las pasarelas símplex y los transmisores de enlace `-L`),
nombre `GB3AA Brist` (indicativo + población), y aeropuertos con tráfico o
con torre (solo RX, AM).

Algunas regiones tienen más de 99 candidatos (SE 113, SW 142, Midlands 113,
North 121). Se quedan los más cercanos a **tu locator**; sin locator, los más
cercanos al centro de la región, dando prioridad a los aeropuertos grandes.
La vista previa dice cuáles no caben.

## Uso

| Dónde | Cómo |
|---|---|
| Windows / Linux | **Herramientas → Codeplug por país…** |
| Android / web | Pestaña **País** |
| Línea de comandos | `RT950Toolkit country ES -o espana.rt950 [--locator IM98IB]` · `RT950Toolkit country GB -i radio.rt950 --mode append -o radio+uk.rt950` |
| Ficheros ya generados | En cada versión: `codeplug-ES-x.y.z.rt950`, `-chirp.csv`, `-rt950editor.csv`, `-zones.csv` (y lo mismo para GB) |

Para una radio ya configurada: **Leer de la radio → Codeplug por país →
Añadir → Escribir en la radio**.

## Datos y su actualización

| Fuente | Licencia / uso | Fichero |
|---|---|---|
| [OurAirports](https://ourairports.com/data/) (aeropuertos y frecuencias) | Dominio público | `tools/country_sources/ourairports_*.csv` |
| [URE — Repetidores y balizas](https://www.ure.es/repetidores/) | Datos publicados por la URE | `tools/country_sources/ure_144_*.txt`, `ure_432_*.txt` |
| [ukrepeater.net](https://ukrepeater.net/csvfiles.html) (RSGB ETCC) | Datos publicados por el ETCC | `tools/country_sources/ukrepeater_net_*.txt` |

`tools/build_country_data.py` genera `pc/rt950_toolkit/data/countries/*.json`
a partir de esas listas y `tools/export_web_meta.py` las copia a la app. Las
listas se copiaron el 2 de octubre de 2026 y se comprobó con una suma de
control que eran idénticas a las publicadas.

Reglas de conversión:

- Las frecuencias aeronáuticas publicadas como **designadores de canal de
  8,33 kHz** (terminadas en 5, 10 o 15 kHz dentro de cada bloque de 25 kHz) se
  convierten a la frecuencia real de la portadora.
- Por aeropuerto: hasta 6 frecuencias en los grandes, 4 en los medianos y 2 en
  los aeródromos, en orden torre, información, aproximación, ATIS, rodadura.
- Solo banda aérea de VHF (108–137 MHz); no se incluyen frecuencias militares
  de UHF, operaciones ni emergencias.
- Las listas públicas tienen errores (p. ej. un locator imposible de la URE);
  esos repetidores se cargan igualmente pero sin posición.

Las listas cambian: comprueba los repetidores y frecuencias de tu zona antes
de usarlos.

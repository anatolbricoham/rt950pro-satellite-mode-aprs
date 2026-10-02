"""i18n.py - English / Spanish user interface strings."""

LANG = "es"

T = {
    "file": ("File", "Archivo"), "new": ("New", "Nuevo"), "open": ("Open...", "Abrir..."),
    "save": ("Save", "Guardar"), "save_as": ("Save as...", "Guardar como..."),
    "import_csv": ("Import CSV (native)...", "Importar CSV (nativo)..."),
    "import_chirp": ("Import CHIRP CSV...", "Importar CSV de CHIRP..."),
    "export_csv": ("Export CSV (native)...", "Exportar CSV (nativo)..."),
    "export_chirp": ("Export CHIRP CSV...", "Exportar CSV de CHIRP..."),
    "import_raw": ("Import raw image...", "Importar imagen binaria..."),
    "export_raw": ("Export raw image...", "Exportar imagen binaria..."),
    "exit": ("Exit", "Salir"),
    "open_dat": ("Open CPS .dat...", "Abrir .dat del CPS..."),
    "save_dat": ("Save as CPS .dat...", "Guardar como .dat del CPS..."),
    "dat_template": ("Choose a CPS .dat to use as template", "Elige un .dat del CPS como plantilla"),
    "import_editor": ("Import RT-950 Editor CSV...", "Importar CSV de RT-950 Editor..."),
    "export_editor": ("Export RT-950 Editor CSV...", "Exportar CSV de RT-950 Editor..."),
    "import_zones": ("Import zones CSV...", "Importar CSV de zonas..."),
    "export_zones": ("Export zones CSV...", "Exportar CSV de zonas..."),
    "import_mod": ("Import FM/AM/SSB CSV...", "Importar CSV FM/AM/SSB..."),
    "export_mod": ("Export FM/AM/SSB CSV...", "Exportar CSV FM/AM/SSB..."),
    "radio": ("Radio", "Radio"),
    "read_radio": ("Read from radio", "Leer de la radio"),
    "write_radio": ("Write to radio", "Escribir en la radio"),
    "write_channels": ("Write channels only", "Escribir solo canales"),
    "restore_backup": ("Restore a backup...", "Restaurar una copia..."),
    "backups": ("Open backups folder", "Abrir carpeta de copias"),
    "tools": ("Tools", "Herramientas"), "stock": ("Add stock channels", "Añadir canales predefinidos"),
    "help": ("Help", "Ayuda"), "about": ("About", "Acerca de"), "language": ("Language", "Idioma"),
    "channels": ("Channels", "Canales"), "zones": ("Zones", "Zonas"), "vfo": ("VFO", "VFO"),
    "settings": ("Settings", "Ajustes"), "aprs": ("APRS", "APRS"), "satellites": ("Satellites", "Satélites"),
    "bootlogo": ("Boot logo", "Logo de arranque"), "log": ("Log", "Registro"),
    "port": ("Port", "Puerto"), "refresh": ("Refresh", "Actualizar"),
    "zone": ("Zone", "Zona"), "all": ("All", "Todas"), "show_empty": ("Show empty", "Mostrar vacíos"),
    "edit": ("Edit", "Editar"), "delete": ("Delete", "Borrar"), "add": ("Add", "Añadir"),
    "copy": ("Copy", "Copiar"), "paste": ("Paste", "Pegar"),
    "up": ("Move up", "Subir"), "down": ("Move down", "Bajar"),
    "ok": ("OK", "Aceptar"), "cancel": ("Cancel", "Cancelar"), "apply": ("Apply", "Aplicar"),
    "name": ("Name", "Nombre"), "rx": ("RX MHz", "RX MHz"), "tx": ("TX MHz", "TX MHz"),
    "rx_tone": ("RX tone", "Tono RX"), "tx_tone": ("TX tone", "Tono TX"), "power": ("Power", "Potencia"),
    "bw": ("Bandwidth", "Ancho"), "scan": ("Scan", "Escaneo"), "tx_en": ("TX enable", "TX permitida"),
    "mod": ("RX mod", "Modo RX"), "busy": ("Busy lock", "Bloqueo ocupado"), "scramble": ("Scramble", "Scrambler"),
    "encr": ("Encryption", "Cifrado"), "signal": ("Signal code", "Código señal"), "pttid": ("PTT-ID", "PTT-ID"),
    "number": ("No.", "Nº"), "duplex": ("Duplex", "Dúplex"),
    "locator": ("Locator", "Locator"), "hours": ("Hours", "Horas"), "min_el": ("Min elev.", "Elev. mín."),
    "update_tle": ("Update TLE", "Actualizar TLE"), "calc_passes": ("Calculate passes", "Calcular pases"),
    "add_doppler": ("Add Doppler channels from no.", "Añadir canales Doppler desde el nº"),
    "open_report": ("Open report", "Abrir informe"),
    "upload_satdb": ("Upload to radio (custom FW)", "Subir a la radio (FW custom)"),
    "set_clock": ("Set radio clock (custom FW)", "Poner en hora la radio (FW custom)"),
    "load_image": ("Load image...", "Cargar imagen..."), "export_bmp": ("Export BMP 240x320...", "Exportar BMP 240x320..."),
    "upload_logo": ("Upload logo (custom FW)", "Subir logo (FW custom)"),
    "callsign": ("Call sign", "Indicativo"), "ssid": ("SSID", "SSID"),
    "unsaved": ("There are unsaved changes. Continue?", "Hay cambios sin guardar. ¿Continuar?"),
    "write_confirm": ("Write the codeplug to the radio?\nA backup of the radio is read and saved first.",
                      "¿Escribir la configuración en la radio?\nAntes se lee y se guarda una copia de seguridad de la radio."),
    "done": ("Done", "Hecho"), "error": ("Error", "Error"),
    "no_port": ("Select the programming cable port.", "Selecciona el puerto del cable de programación."),
    "aprs_msg_note": ("Text messaging with acknowledgements (inbox, compose, auto-ack) runs in the custom firmware: "
                      "menu APRS Set > Messages. With the stock firmware only the beacon custom message is available.",
                      "La mensajería de texto con acuse (bandeja, redactar, ack automático) funciona en el firmware "
                      "custom: menú APRS Set > Messages. Con el firmware original solo existe el mensaje personalizado de la baliza."),
    "firmware": ("Firmware", "Firmware"),
    "fw_published": ("Published firmware", "Firmware publicado"),
    "fw_refresh": ("Check online", "Buscar en línea"),
    "fw_from_file": ("From file...", "Desde fichero..."),
    "fw_selected": ("Selected", "Seleccionado"),
    "fw_in_bootloader": ("Radio already in bootloader mode (two lower side keys held at power on)",
                         "La radio ya está en modo cargador (dos teclas laterales inferiores al encender)"),
    "fw_flash": ("Flash firmware", "Flashear firmware"),
    "fw_web": ("Open the web flasher", "Abrir el flasheador web"),
    "fw_confirm": ("Write this firmware to the radio?\n\n%s\n\nDo not disconnect the cable or switch the radio off "
                   "until it finishes. The current firmware cannot be read back: keep the original .BTF file.",
                   "¿Escribir este firmware en la radio?\n\n%s\n\nNo desconectes el cable ni apagues la radio hasta "
                   "que termine. El firmware actual no se puede leer: guarda el fichero .BTF original."),
    "fw_note": ("Custom firmware: experimental. To go back to the stock firmware, flash the official Radtel .BTF "
                "(see Help → Backups).",
                "Firmware custom: experimental. Para volver al firmware original, flashea el .BTF oficial de "
                "Radtel (ver Ayuda → Copias de seguridad)."),
    "fw_none": ("No firmware selected", "No hay firmware seleccionado"),
    "fw_offline": ("Could not reach GitHub: %s", "No se pudo conectar con GitHub: %s"),
    "country": ("Country codeplug...", "Codeplug por país..."),
    "country_title": ("Preloaded country codeplug", "Codeplug precargado por país"),
    "country_mode": ("What to do with the channels already in the codeplug", "Qué hacer con los canales que ya hay"),
    "mode_overwrite": ("Overwrite: replace all channels (new radio)", "Sobrescribir: sustituir todos los canales (radio nueva)"),
    "mode_append": ("Add: keep my channels and fill the free slots of each zone",
                    "Añadir: conservar mis canales y rellenar los huecos libres de cada zona"),
    "rename_zones": ("Rename the zones (EA1, EA2... / UK regions)", "Renombrar las zonas (EA1, EA2... / regiones UK)"),
    "include": ("Include", "Incluir"),
    "k_repeater": ("Amateur FM repeaters", "Repetidores FM de radioaficionado"),
    "k_airport": ("Airports (RX only, AM)", "Aeropuertos (solo RX, AM)"),
    "k_simplex": ("Simplex / APRS / ISS", "Símplex / APRS / ISS"),
    "k_pmr": ("PMR446 (RX only)", "PMR446 (solo RX)"),
    "k_cb": ("CB 27 MHz (RX only)", "CB 27 MHz (solo RX)"),
    "my_locator": ("My locator (nearest channels first when a zone is full)",
                   "Mi locator (si una zona se llena, se quedan los más cercanos)"),
    "preview": ("Preview", "Vista previa"),
    "country_hint": ("Tip: on a configured radio, first Read from radio, then Add, then Write. Only channels and "
                     "zone names are written; the radio settings are not changed.",
                     "Consejo: en una radio ya configurada, primero Leer de la radio, luego Añadir y después "
                     "Escribir. Solo se escriben canales y nombres de zona; los ajustes de la radio no cambian."),
    "help_manual": ("User manual", "Manual de uso"),
    "help_backup": ("Backups (firmware and configuration)", "Copias de seguridad (firmware y configuración)"),
    "help_disclaimer": ("Disclaimer", "Aviso de responsabilidad"),
    "help_online": ("Online documentation", "Documentación en línea"),
    "help_web": ("Project web site", "Web del proyecto"),
    "accept": ("I understand and accept", "Entiendo y acepto"),
    "decline": ("Exit", "Salir"),
    "credits": ("Credits", "Créditos"),
    "experimental": ("experimental", "experimental"),
    "vfo_ro": ("VFO options are shown read-only (layout not confirmed on hardware). Change them on the radio.",
               "Las opciones del VFO se muestran solo lectura (mapa no confirmado en hardware). Cámbialas en la radio."),
    "rx_only_sat": ("RX only", "solo RX"),
}


def _(key: str) -> str:
    v = T.get(key)
    if not v:
        return key
    return v[1] if LANG == "es" else v[0]


def set_lang(lang: str):
    global LANG
    LANG = "es" if lang.lower().startswith("es") else "en"

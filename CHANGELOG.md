# Changelog

## V4.7 – Alpha / Web Control

- Weboberfläche als Hauptbedienung erweitert.
- Dashboard mit Temperatur, Modus, Heizstufe, Reglerleistung und Safety-Status.
- OEM/AUTO/BOOST/MANUAL über Browser.
- Solltemperatur, Totband und Restart-Differenz im Web einstellbar.
- DS18B20-Rollenzuweisung und Offsets im Web.
- Regler-, Heizstufen- und Safety-Parameter im Web.
- Lernsystem im Web.
- Diagnose und kontrollierter Fault-Reset.
- Konfigurations-Backup und Restore.
- Firmware-/Build-Anzeige.
- Geschütztes OTA-Firmwareupdate bleibt erhalten.
- Vor OTA werden eigene Heizanforderung und H1/H2/H3 abgeschaltet.
- V4.6 bleibt als Rückfallversion erhalten.
- Status: **Alpha / Testing only**.


## V4.6 – Alpha

- WLAN Access Point `ZHC`
- Terminal WLAN on/off control
- Configurable WLAN startup behavior
- Password-protected local web interface
- Generated first-login password
- Mandatory password change after first login
- Salted SHA-256 password storage
- Login attempt limiting
- Session timeout
- Local credential recovery
- Authenticated OTA firmware update
- Safe OTA transition to OEM with H1/H2/H3 LOW
- Hierarchical terminal UI retained
- Multi-sensor cabin control retained

## V4.5 – Alpha

- Hierarchical terminal menu and submenus
- Expert text commands retained

## V4.4 – Alpha

- Terminal kept available during heater Safety-Lock
- Missing sensor assignment after flash treated as setup state
- Dedicated analysis workflow

## V4.3 – Alpha

- Flexible DS18B20 role assignment
- CABIN_LOW / CABIN_HIGH / OUTSIDE / SAFETY
- Virtual cabin temperature
- Stratification handling

## Earlier Alpha development

Earlier revisions introduced the PI controller, stage management, thermal
learning, RGB status, fault handling and bench logic outputs.
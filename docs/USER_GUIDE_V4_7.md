# ZHC V4.7 – Bedienungsanleitung

> [!WARNING]
> **ALPHA SOFTWARE – EXPERIMENTAL / TESTING ONLY**
>
> ZHC V4.7 ist nicht als serienreifes oder fahrzeugzertifiziertes Steuergerät freigegeben.
> Tests, Einbau und Nutzung erfolgen auf eigene Verantwortung.
> H1/H2/H3 sind ausschließlich **3,3-V-Logik-/Bench-Ausgänge**.
> Keine direkte Verbindung zu Hochvolt-, PTC-Leistungs- oder HV-Schützpfaden.

## Hardware / GPIO

| Funktion | GPIO |
|---|---:|
| Taster | GPIO3 |
| RGB Rot | GPIO4 |
| RGB Grün | GPIO5 |
| RGB Blau | GPIO6 |
| DS18B20 1-Wire DATA | GPIO7 |
| H1 | GPIO8 |
| H2 | GPIO9 |
| H3 | GPIO10 |

Alle DS18B20 liegen parallel auf einem gemeinsamen 1-Wire-Bus an GPIO7.
Zusätzlich werden 3,3 V und GND zu allen Sensoren geführt. Ein gemeinsamer 4,7-kΩ-Pull-up liegt zwischen DATA und 3,3 V.

Sensorrollen:
- CABIN_LOW
- CABIN_HIGH
- OUTSIDE
- SAFETY (optional)

## WLAN

SSID:

```text
ZHC
```

Standard-IP:

```text
192.168.4.1
```

Das WLAN-Passwort wird automatisch erzeugt und lokal im seriellen Terminal angezeigt.

Terminal:

```text
wifi on
wifi status
wifi startup on
wifi startup off
```

## Web-Login

Adresse:

```text
http://192.168.4.1
```

Benutzer:

```text
admin
```

Beim ersten Start wird ein temporäres Initialpasswort erzeugt und im seriellen Terminal ausgegeben.
Beim ersten Login muss ein eigenes Administrator-Passwort gesetzt werden.

Zur lokalen Wiederherstellung:

```text
security reset
```

Alternativ kann der Taster beim Booten etwa 8 Sekunden gehalten werden.

## Dashboard

Das Dashboard zeigt:
- Innenraumtemperatur
- Solltemperatur
- Betriebsart
- Heizstufe
- Reglerleistung
- H1/H2/H3-Status
- Safety-Lock
- Fehler
- Warnung

## Bedienung

Verfügbare Betriebsarten:
- OEM
- AUTO
- BOOST
- MANUAL

Einstellbar:
- Solltemperatur
- Totband
- Wiedereinschaltdifferenz
- manuelle Stufe 0–3

AUTO, BOOST und MANUAL starten nur, wenn die Sensorik und Safety-Bedingungen gültig sind.

## Sensoren

Die Weboberfläche kann gefundene DS18B20 den Rollen zuordnen:
- CABIN_LOW
- CABIN_HIGH
- OUTSIDE
- SAFETY

Pro Sensorrolle ist ein Offset einstellbar.

## Regler

Einstellbar:
- Kp
- Ki
- Trend Gain
- Schwellen für Stufe 1/2/3
- Mindestzeit einer Stufe
- Stufenverzögerung
- Boost-Dauer
- Safety-Maximalabweichung
- Safety-Verzögerung
- Not-Aus-Temperatur
- Sensor-Timeout
- maximaler Temperatursprung

Ungültige Konfigurationen werden nicht gespeichert.

## Lernsystem

Einstellbar:
- Lernsystem EIN/AUS
- Lernrate
- Mindest-Lernzeit

Angezeigt werden die gelernten Werte für Heizstufe 1, 2, 3 und Coast.
Die Lernwerte können zurückgesetzt werden.

## Diagnose

Angezeigt werden:
- Safety-Lock
- aktiver Fehler
- aktive Warnung
- Reglerausgang
- angeforderte Stufe
- aktive Stufe

Ein Fault-Reset kann über die Weboberfläche ausgelöst werden.
Der Safety-Lock wird nur aufgehoben, wenn die Safety-Bedingungen wieder gültig sind.

## Backup / Restore

Unter System kann die aktuelle Konfiguration als

```text
ZHC_config_backup.txt
```

gesichert werden.

Gesichert werden u. a.:
- Temperatur- und Reglerwerte
- Heizstufenschwellen
- Lernparameter
- Lernwerte
- Safety-Einstellungen
- Sensoradressen
- Sensor-Offsets

Passwörter werden nicht im Backup gespeichert.

Beim Restore wird die Konfiguration geprüft. Danach bleibt der Controller im OEM-Modus und H1/H2/H3 bleiben AUS.

## OTA-Firmwareupdate

Unter Update kann eine kompilierte ESP32-C3-`.bin`-Datei hochgeladen werden.

Vor dem Update werden automatisch:
- OEM-Modus gesetzt
- H1 AUS
- H2 AUS
- H3 AUS
- Reglerausgang auf 0 % gesetzt

Nach erfolgreichem Update startet der ESP32 neu.

## Terminal-Fallback

Baudrate:

```text
115200
```

Wichtige Befehle:

```text
menu
status
analyse
diag
thermal

sensor scan
sensor setup
sensor status

mode oem
mode auto
mode boost
mode manual

target 21.0
stage 0

wifi on
wifi off
wifi status
wifi startup on
wifi startup off

security reset

save
defaults
reboot
```

## Status

V4.7 bleibt zunächst **Alpha / Testing only**.
V4.6 bleibt als Rückfallversion im Repository erhalten.

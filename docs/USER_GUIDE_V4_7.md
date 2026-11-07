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


## Service-Diagnose (V4.8)

Ab V4.8 gibt es den zusätzlichen Menüpunkt **Service**.

### H1 / H2 / H3 einzeln testen

- H1 = GPIO8
- H2 = GPIO9
- H3 = GPIO10

Jeder Ausgangstest läuft maximal **2 Sekunden** und endet automatisch.

Vor jedem Test setzt ZHC:
- Betriebsart auf OEM
- angeforderte Stufe auf 0
- aktive Stufe auf 0
- Reglerausgang auf 0 %
- Integrator zurück

Bei Safety-Lock, FAILSAFE oder laufendem OTA-Update sind die Ausgangstests gesperrt.

Zusätzlich gibt es **ALLE AUS**.

### RGB-Test

Rot, Grün und Blau können jeweils für 3 Sekunden geprüft werden.

### Taster

GPIO3 wird angezeigt:
- HIGH = frei
- LOW = gedrückt

### DS18B20

Der 1-Wire-Bus an GPIO7 kann neu gescannt werden.
Gefundene Sensoren werden mit Index und ROM-Adresse angezeigt.

### Systemstatus

Die Seite zeigt zusätzlich:
- Modus
- Safety-Lock
- Fehler
- Reglerausgang
- WLAN-Status
- IP-Adresse

> Die Service-Ausgangstests sind ausschließlich für 3,3-V-/Niedervolt-Bench-Prüfungen vorgesehen.


## PWM-Status-LED und OEM-Sofortabschaltung (V4.8.2)

### RGB-LED

Die RGB-LED wird ab V4.8.2 per PWM angesteuert.

Belegung:
- Rot: GPIO4
- Grün: GPIO5
- Blau: GPIO6

Aktuelle Statusdarstellung:
- **OEM:** langsam blau atmend
- **AUTO:** grün
- **BOOST:** gelb/orange pulsierend
- **MANUAL:** cyan
- **Service-Ausgangstest:** violett pulsierend
- **Warnung:** gelb blinkend
- **Fehler / Safety-Lock:** rot blinkend
- **OTA-Update:** schnell blau pulsierend

Die maximale LED-Helligkeit ist softwareseitig begrenzt, damit die Anzeige im Fahrzeug nicht unnötig hell ist.

### Sofortige Abschaltung bei OEM

Beim Wechsel von AUTO, BOOST oder MANUAL auf **OEM** werden die eigenen Ausgänge jetzt ohne Stufenabbau sofort abgeschaltet:

- H1 = AUS
- H2 = AUS
- H3 = AUS
- angeforderte Stufe = 0
- aktive Stufe = 0
- manuelle Stufe = 0
- Reglerausgang = 0 %
- Integrator = 0
- Lern-/Reglerzustand wird auf einen sicheren Ausgangszustand gesetzt

Die Mindest-Stufenzeit und Stufenverzögerung werden bei einer OEM-Abschaltung nicht angewendet.

### Zentraler Abschaltpfad

Der gleiche unmittelbare Abschaltpfad wird für folgende Situationen verwendet:
- OEM
- Fehler / FAILSAFE
- OTA-Update
- Service STOP / ALLE AUS

Damit sollen H1/H2/H3 in allen Abschaltsituationen konsistent behandelt werden.

> H1/H2/H3 sind weiterhin ausschließlich 3,3-V-Logik-/Niedervolt-Bench-Ausgänge.


## LED-Stufenanzeige (V4.8.3)

Ab V4.8.3 bleibt die jeweilige Modusfarbe als Grundlicht erhalten. Zusätzlich zeigt die RGB-LED die aktive Heizstufe durch helle Pulse an:

- **Stufe 0:** keine Zusatzimpulse
- **Stufe 1:** 1 heller Impuls
- **Stufe 2:** 2 helle Impulse
- **Stufe 3:** 3 helle Impulse

Das Pulsmuster wiederholt sich ungefähr alle 4 Sekunden.

### Grundfarben

- **OEM:** blau atmend
- **AUTO:** grün
- **BOOST:** orange
- **MANUAL:** cyan

### Prioritäten

Fehler-, Warn-, OTA- und Serviceanzeigen haben weiterhin Vorrang vor der normalen Modus-/Stufenanzeige.

Der lange Tastendruck wurde in V4.8.3 nicht verändert.

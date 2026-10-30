# ZHC – ZOE Climate Controller

> [!CAUTION]
> ## ALPHA SOFTWARE – EXPERIMENTAL / TESTING ONLY
>
> Dieses Projekt befindet sich ausdrücklich im **Alpha-Status**.
> Es handelt sich um experimentelle Entwicklungssoftware und nicht um ein
> serienreifes, sicherheitszertifiziertes oder für den Straßenverkehr
> freigegebenes Fahrzeugsteuergerät.
>
> **Jeder Test, jede Installation und jede Verwendung erfolgt ausschließlich
> auf eigene Verantwortung und eigenes Risiko.**
>
> Der Autor und Mitwirkende übernehmen keine Haftung für Schäden an Fahrzeugen,
> Elektronik, Komponenten, Daten oder sonstige Folgeschäden, soweit dies
> gesetzlich zulässig ist.
>
> **H1/H2/H3 sind in dieser Alpha-Version ausschließlich als 3,3-V-Logik-/
> Bench-Signale vorgesehen.** Sie sind nicht zur direkten Ansteuerung von
> Hochvolt-, PTC-, Schütz- oder Leistungskomponenten vorgesehen.

## Projektstatus

- **Firmware:** V4.6
- **Status:** ALPHA / DEVELOPMENT / EXPERIMENTAL
- **Controller:** ESP32-C3 Super Mini
- **Projekt:** experimenteller Innenraum-Klimaregler / Entwicklungsplattform

## Funktionen

- hierarchisches serielles Terminal-Menü
- OEM / AUTO / BOOST / MANUAL
- adaptive PI-Regelung
- drei logische Heizstufen
- Mindestschaltzeiten und Stufenverzögerung
- thermisches Lernmodell
- mehrere DS18B20-Sensoren
- freie Rollen: `CABIN_LOW`, `CABIN_HIGH`, `OUTSIDE`, `SAFETY`
- gewichtete virtuelle Innenraumtemperatur
- Auswertung der Temperaturschichtung
- Sensor-Offsets und Sensorzuordnung
- Safety-Lock / FAILSAFE
- Fehlerhistorie und Diagnose
- WLAN Access Point `ZHC`
- WLAN über Terminal ein-/ausschaltbar
- geschütztes Webinterface
- Erstpasswort mit Passwortwechsel
- Session-Timeout und Login-Limit
- lokaler Credential-Recovery
- geschütztes OTA-Firmwareupdate
- sicherer OTA-Zustand: OEM / H1-H3 AUS

## GPIO – aktueller Alpha-Stand

| Funktion | GPIO |
|---|---:|
| Taster | 3 |
| RGB Rot | 4 |
| RGB Grün | 5 |
| RGB Blau | 6 |
| DS18B20 OneWire | 7 |
| H1 Logikausgang | 8 |
| H2 Logikausgang | 9 |
| H3 Logikausgang | 10 |

> [!WARNING]
> Die GPIO-Zuordnung ist Teil des Alpha-Standes und kann sich ändern.
> Die konkrete ESP32-C3-Boardvariante muss vor Tests geprüft werden.

## Arduino-Abhängigkeiten

- ESP32 Arduino Core
- OneWire
- DallasTemperature
- Preferences
- WiFi
- WebServer
- Update
- mbedTLS SHA-256

Empfohlene IDE-Einstellungen des aktuellen Teststands:

- Board: `ESP32C3 Dev Module`
- Serial Monitor: `115200 Baud`
- `USB CDC On Boot`: `Enabled`

## Sicherer Erststart

Nach dem Flashen startet ZHC bewusst im sicheren Zustand:

- Modus `OEM`
- H1/H2/H3 `AUS`
- keine automatische Heizfreigabe
- ohne Innenraumsensor bleibt das Gerät im Setup-/OEM-Zustand
- das Terminal bleibt bedienbar

## Sensoren

Beispiel:

```text
sensor scan
sensor setup

assign low 0
assign high 1
assign outside 2
assign safety none

save
```

Nicht zugewiesene Rollen sind deaktiviert.

`CABIN_LOW` und `CABIN_HIGH` dürfen unterschiedliche Temperaturen messen.
Diese Schichtung ist normal und wird nicht als Fehler behandelt.

## WLAN

SSID:

```text
ZHC
```

Terminal:

```text
wifi on
wifi off
wifi status
wifi startup on
wifi startup off
```

## Webinterface

- Benutzer: `admin`
- temporäres Erstpasswort wird lokal im Terminal angezeigt
- beim ersten Login ist ein neues Passwort erforderlich
- das gesetzte Passwort wird nicht im Klartext gespeichert
- Session-Timeout und Login-Limit sind aktiv

## Credential-Recovery

Terminal:

```text
security reset
```

Zusätzlich ist lokaler Recovery über den Gerätetaster beim Boot vorgesehen.
Dabei werden nur Web-Zugangsdaten zurückgesetzt; Sensor- und Reglerwerte
bleiben erhalten.

## OTA

Vor dem Firmware-Update:

1. Modus auf OEM
2. Heizanforderung auf 0
3. H1/H2/H3 AUS
4. Firmware schreiben
5. Neustart
6. erneuter Start im OEM-Zustand

## Sicherheits-Hinweis

Diese Firmware ersetzt **keine** fahrzeugseitigen Sicherheitsmechanismen,
Verriegelungen, Diagnosefunktionen oder Schutzschaltungen.

Der aktuelle Alpha-Stand enthält unter anderem:

- Fail-low-Ausgänge
- Sensorplausibilisierung
- Sensor-Timeout
- Übertemperaturgrenze
- Regler-Plausibilisierung
- Safety-Lock
- kein automatischer Wiederanlauf nach Fehler
- manueller Fehler-Reset
- Fehlerhistorie
- sicheren Boot- und OTA-Zustand

Dies ist **kein zertifiziertes funktionales Sicherheitskonzept**.

## Testregeln

> [!IMPORTANT]
> Tests sollten zuerst ausschließlich am sicheren Niedervolt-/Bench-Aufbau
> erfolgen.

- Keine direkte Verbindung der ESP32-GPIOs mit Hochvolt- oder Leistungskomponenten.
- OEM-Schutz- und Verriegelungsfunktionen dürfen nicht außer Kraft gesetzt werden.
- Ausgangszustände vor jedem Test prüfen.
- Bei unklarem Verhalten sofort in OEM-/Safe-Zustand zurückkehren.
- Firmwareänderungen zuerst am Bench-Aufbau testen.

## Repository

```text
firmware/
  Zoe_Heater_Controller_V4_6.ino

docs/
  SAFETY.md
  TESTING.md

CHANGELOG.md
README.md
.gitignore
```

## Fehlerberichte

Bitte angeben:

- Firmwareversion
- ESP32-C3-Boardvariante
- Sensorbestückung
- relevante Terminalausgabe
- Schritte zum Reproduzieren
- erwartetes Verhalten
- tatsächliches Verhalten

Keine Passwörter, WLAN-Schlüssel oder sonstige Zugangsdaten veröffentlichen.

## Haftungsausschluss

Dieses Projekt wird ohne Zusicherungen oder Garantien bereitgestellt.
Die Nutzung und Erprobung erfolgt ausschließlich auf eigene Verantwortung.
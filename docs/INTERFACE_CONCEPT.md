# Possible Vehicle Interface Concept

> [!CAUTION]
> ## ALPHA / CONCEPT ONLY
>
> Dieses Dokument beschreibt **nur ein mögliches Low-Voltage-Interface-Konzept**.
> Es ist **keine Anleitung zum direkten Ansteuern von Hochvolt-PTC-Heizern,
> HV-Schützen oder zum Umgehen von Renault-Sicherheitsfunktionen**.
>
> Fahrzeugseitige Freigaben, Verriegelungen, Diagnose und Schutzfunktionen
> müssen erhalten bleiben.
>
> Tests erfolgen ausschließlich auf eigene Verantwortung.

## Ziel

ZHC stellt aktuell nur drei 3,3-V-Logiksignale bereit:

- H1
- H2
- H3

Diese Signale dürfen **nicht direkt** an Fahrzeugrelais, PTC-Leistungspfade
oder Hochvolt-Komponenten angeschlossen werden.

Zwischen ESP32-C3 und Fahrzeug sollte ein separates, fehlertolerantes
Low-Voltage-Interface sitzen.

## Empfohlenes Architekturprinzip

```text
ESP32-C3 / ZHC
      |
      | 3.3-V Logik
      v
+-------------------+
| galvanische       |
| Trennung          |
| / Optokoppler     |
+-------------------+
      |
      v
+-------------------+
| Automotive Driver |
| Fail-Low          |
| Strombegrenzung   |
| Diagnose          |
+-------------------+
      |
      v
+-------------------+
| OEM-kompatible    |
| Low-Voltage       |
| Steuerschnittstelle|
+-------------------+
      |
      v
OEM-Relais / OEM-Steuerpfad
```

## Grundprinzipien

### 1. ESP32 nie direkt an eine Relaisspule

Die GPIOs des ESP32-C3 sind reine Logikausgänge.

Ein Fahrzeugrelais kann:

- deutlich mehr Strom benötigen,
- Spannungsspitzen erzeugen,
- EMV-Störungen verursachen,
- beim Fehler einen undefinierten Zustand erzeugen.

Darum ist eine getrennte Treiberstufe erforderlich.

### 2. Fail-Low

Der sichere Defaultzustand muss sein:

```text
ESP AUS
Controller Reset
Watchdog
Kabelbruch
Treiberfehler

=> Heizanforderung AUS
```

Ein Ausgang darf nicht durch einen Reset oder Floating-Zustand unbeabsichtigt
aktiv werden.

### 3. Galvanische Trennung

Eine mögliche Architektur ist eine galvanische Trennung zwischen ESP32 und
Fahrzeugseite.

Beispiel:

```text
GPIO H1
   |
   v
Optokoppler / Digital Isolator
   |
   v
Low-Side- oder High-Side-Treiber
   |
   v
OEM Low-Voltage Eingang
```

Die Trennung reduziert das Risiko von:

- Masseschleifen
- Spannungsspitzen
- Störungen aus dem Bordnetz
- Rückspeisung in den ESP32

### 4. Automotive-taugliche Treiberstufe

Auf der Fahrzeugseite sollte eine dafür geeignete Treiberstufe verwendet
werden.

Wichtige Eigenschaften:

- definierter AUS-Zustand
- Überstromschutz
- Überspannungsschutz
- Temperaturüberwachung
- Diagnosemöglichkeit
- Schutz gegen induktive Lasten
- geeignete Bordnetz-Spannungsfestigkeit

## Mögliche Low-Voltage-Topologie

Nur als Blockdiagramm:

```text
                +----------------------+
3.3 V H1 ------>| Isolation Channel 1  |----> Driver 1 ----> OEM Control 1
3.3 V H2 ------>| Isolation Channel 2  |----> Driver 2 ----> OEM Control 2
3.3 V H3 ------>| Isolation Channel 3  |----> Driver 3 ----> OEM Control 3
                +----------------------+

                         |
                         +--> Driver Enable
                         |
                         +--> Hardware Watchdog
                         |
                         +--> Emergency Disable
```

## Hardware-Enable

Zusätzlich zu H1/H2/H3 ist ein separater Hardware-Enable sinnvoll.

```text
ENABLE = 0
=> alle drei Fahrzeugausgänge AUS

ENABLE = 1
=> H1/H2/H3 dürfen ausgewertet werden
```

Damit kann ein unabhängiger Watchdog die komplette Heizanforderung abschalten.

## Watchdog-Konzept

```text
ESP32 sendet Heartbeat
        |
        v
Hardware Watchdog
        |
        +---- Heartbeat OK ---> Driver Enable
        |
        +---- Timeout --------> Driver AUS
```

Der Watchdog sollte unabhängig von der normalen Regelschleife arbeiten.

## Versorgung

Die ESP32-Versorgung sollte nicht direkt ungeschützt am Fahrzeugbordnetz
betrieben werden.

Mögliche Architektur:

```text
12-V Bordnetz
     |
     v
Sicherung
     |
     v
Automotive DC/DC
     |
     +----> 5 V
              |
              v
          ESP32-C3
```

Zusätzlich sind Schutzmaßnahmen gegen Bordnetztransienten erforderlich.

## Diagnose-Rückführung

Professioneller wäre eine Rückmeldung der tatsächlichen Ausgangszustände.

```text
ZHC Request H1
      |
      v
Driver
      |
      v
OEM Control
      |
      +----> Feedback / Sense
                 |
                 v
              ESP32
```

Dann kann die Software unterscheiden zwischen:

```text
ANGEFORDERT = EIN
RUECKMELDUNG = EIN
=> OK

ANGEFORDERT = EIN
RUECKMELDUNG = AUS
=> Driver-/Leitungsfehler

ANGEFORDERT = AUS
RUECKMELDUNG = EIN
=> kritischer Ausgangsfehler
```

## Was zuerst verifiziert werden muss

Bevor irgendeine Fahrzeuganbindung entwickelt wird, muss zweifelsfrei
feststehen:

1. Welche Zoe-Leitungen tatsächlich Low-Voltage-Steuersignale sind.
2. Ob die Relais low-side oder high-side geschaltet werden.
3. Welche Spannung und welcher Strom an der Steuerschnittstelle auftreten.
4. Ob die Leitung von einem OEM-Steuergerät überwacht wird.
5. Ob Diagnose, Strommessung oder Plausibilisierung vorhanden ist.
6. Welche Fahrzeugfreigaben vor einer Heizanforderung erforderlich sind.

Ohne diese Informationen darf eine Schaltung nicht direkt angeschlossen werden.

## Nicht empfohlen

Nicht verwenden:

```text
ESP32 GPIO ----> Zoe Relaisspule
```

oder:

```text
ESP32 ----> direkte HV-PTC-Ansteuerung
```

oder eine Schaltung, die OEM-Freigaben und Sicherheitsfunktionen dauerhaft
überbrückt.

## Entwicklungsreihenfolge

Empfohlene Reihenfolge:

1. H1/H2/H3 nur mit LEDs testen.
2. Danach Logikanalysator / Oszilloskop.
3. Isolierte Interface-Platine entwickeln.
4. Interface zunächst mit ungefährlicher Testlast prüfen.
5. Fehlerfälle simulieren:
   - ESP Reset
   - Spannungsverlust
   - Watchdog Timeout
   - Kabelunterbrechung
   - Ausgang kurzgeschlossen
6. Erst danach eine reale Low-Voltage-Fahrzeugschnittstelle untersuchen.

## Status

Dieses Interface ist derzeit **nur ein Architekturvorschlag**.

Die konkrete Renault-ZOE-Relaisbelegung, Schaltlogik und elektrische
Spezifikation ist in diesem Repository derzeit **nicht als verifiziert
dokumentiert**.

Bevor daraus eine reale Schnittstelle entsteht, müssen die entsprechenden
Fahrzeugschaltpläne und Messwerte verifiziert werden.


# Konkretes GPIO -> MOSFET -> Relais Interface

> [!WARNING]
> Diese Schaltung ist als **Niedervolt-Relais-Treiber** gedacht.
> Sie beschreibt nicht die direkte Ansteuerung eines Hochvolt-PTC,
> HV-Schützes oder anderer Hochenergie-Komponenten.

## Ein Kanal

Für jeden Ausgang H1/H2/H3 kann dieselbe Low-Side-Treiberstufe verwendet werden:

```text
ESP32-C3 GPIO
     |
     | 100...330 Ohm
     v
   Gate
    |
    +---- 10 kOhm ---- GND
    |
  N-MOSFET
  logic-level
    |
 Source
    |
   GND

Drain --------------------+
                          |
                      Relaisspule
                          |
                         +12 V
```

Parallel zur Relaisspule:

```text
                +12 V
                  |
                  +------|<|------+
                  |   Flyback     |
                  |    diode      |
                  |               |
             Relaisspule          |
                  |               |
                  +------ Drain ---+
```

Orientierung der Freilaufdiode:

- Kathode an +12 V
- Anode an MOSFET-Drain / Relaisspulen-Minus

Im normalen Betrieb sperrt die Diode.
Beim Abschalten der Spule begrenzt sie die induzierte Spannungsspitze.

## Vereinfachtes Schaltbild

```text
ESP32-C3
GPIO H1
  |
 [220R]
  |
  +---------- Gate Q1
  |             |
 [10k]         N-MOSFET
  |             |
 GND          Source
                |
               GND

               Drain
                 |
                 +--------- Relay Coil -------- +12V
                 |                               |
                 +---------|<|------------------+
                         Flyback diode
```

## Relaiskontakt

Die MOSFET-Stufe schaltet ausschließlich die **Relaisspule**.

Die eigentliche Last liegt elektrisch getrennt am Relaiskontakt:

```text
MOSFET-Seite:

ESP32 -> MOSFET -> Relaisspule


Kontaktseite:

Versorgung/Signal ---- COM
                       |
                     Relay
                       |
                       NO ---- Last / OEM Low-Voltage Signal
```

Damit fließt der Laststrom nicht durch den ESP32 oder GPIO.

## Drei Kanäle

```text
GPIO H1 -> MOSFET Q1 -> Relay K1
GPIO H2 -> MOSFET Q2 -> Relay K2
GPIO H3 -> MOSFET Q3 -> Relay K3
```

Jeder Kanal sollte besitzen:

- Gate-Widerstand
- Gate-Pulldown
- Logic-Level N-MOSFET
- Freilaufdiode an der Relaisspule
- abgesicherte 12-V-Versorgung
- definierte gemeinsame Masse, wenn keine galvanische Trennung verwendet wird

## Warum Gate-Pulldown?

Der Pulldown stellt sicher:

```text
ESP32 Reset
GPIO hochohmig
Bootphase
Firmwarefehler

=> MOSFET AUS
=> Relais AUS
```

Das unterstützt das Fail-Low-Konzept.

## MOSFET-Anforderungen

Der MOSFET muss bei **3,3 V Gate-Spannung sicher durchschalten**.

Wichtige Kriterien:

- Logic-Level MOSFET
- ausreichende Drain-Source-Spannungsfestigkeit
- ausreichender Strom für die Relaisspule
- niedriger RDS(on) bei 2,5...3,3 V Gate-Spannung
- genügend Reserve für automotive Niedervolt-Umgebung

Ein MOSFET, der nur mit 10 V Gate-Spannung spezifiziert ist, ist für einen
direkten ESP32-GPIO ungeeignet.

## Fahrzeugseitige Schutzmaßnahmen

Bei Versorgung aus dem 12-V-Bordnetz sollte zusätzlich ein Schutzblock
vorgesehen werden:

```text
12 V Fahrzeug
     |
   Sicherung
     |
 Verpolschutz
     |
 TVS / Transientenschutz
     |
     +---- Relaisspulen
     |
     +---- DC/DC -> ESP32
```

## Optional: galvanische Trennung

Wenn die Fahrzeugseite elektrisch getrennt werden soll:

```text
ESP32 GPIO
    |
 Optokoppler
    |
 MOSFET-Treiber
    |
 Relaisspule
```

Dann muss die Fahrzeugseite eine eigene Versorgung und Masse besitzen.

## Wichtig bei Relaisauswahl

Vor Anschluss an ein reales Fahrzeug muss geprüft werden:

- Spulenspannung des Relais
- Spulenstrom
- Kontaktstrom
- Schaltspannung
- Lastart
- Einschaltstrom
- induktive oder kapazitive Last
- OEM-Diagnoseüberwachung

Der Kontakt muss für die tatsächliche **Niedervolt-Last** ausgelegt sein.

## Was diese Schaltung ausdrücklich nicht bedeutet

Nicht:

```text
ESP32 -> MOSFET -> Hochvolt-PTC
```

Nicht:

```text
ESP32 -> MOSFET -> HV-Schütz ohne OEM-Freigabe
```

Die hier beschriebene Stufe ist nur ein möglicher **12-V-Relais-Treiber für
Low-Voltage-Steuersignale oder Bench-Tests**.


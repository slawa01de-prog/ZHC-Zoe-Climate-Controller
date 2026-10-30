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

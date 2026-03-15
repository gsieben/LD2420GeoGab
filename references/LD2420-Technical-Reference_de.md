# LD2420 Radar Serieller Port Protokoll

---

## Frame-Struktur

- **Frame-Header:** 4 Bytes (`FD FC FB FA`)
- **Datenlänge:** 2 Bytes (Little-Endian)
- **Daten:** Befehl + Nutzdaten
- **Frame-Ende:** 4 Bytes (`04 03 02 01`)
- **Byte-Reihenfolge:** Little-Endian

---

## Wichtige Hinweise

- **Max. Befehlsgröße:** 64 Bytes pro Frame
- **Vor Befehlen:** Config-Modus aktivieren (CMD 0x00FF) → Cache leeren (100ms) → Config-Modus erneut aktivieren

---

# Befehle

* Konfiguration aktivieren/deaktivieren: `0x00FF` / `0x00FE`
* ABD-Parameter lesen/schreiben: `0x0008` / `0x0007`
* Systemparameter lesen/schreiben: `0x0013` / `0x0012`
* Version lesen: `0x0000`
* Neustart / Werksreset: `0x0068`

> **Hinweis:** Register-Befehle `0x0001` (Schreiben) und `0x0002` (Lesen) sind in der offiziellen HLK-Dokumentation vorhanden, aber auf fw v1.6.1 **obsolet** — Schreiben gibt Fehlerstatus `0x0001` zurück, Werte sind nicht mit ABD-Parametern synchronisiert. Ausschließlich ABD-Befehle verwenden.

> **Hinweis:** Seriennummer lesen `0x0011` ist dokumentiert, aber auf fw v1.6.1 **nicht unterstützt** — keine Antwort.

---

# 1. Activity-Based Detection (ABD)

> ABD-Parameter stehen für „Activity-Based Detection"-Parameter. Diese steuern die Empfindlichkeit und das Verhalten des LD2420-Radars für Bewegungs- und Anwesenheitserkennung. Sie umfassen Schwellenwerte (hoch/niedrig) für jedes Gate (Zone) sowie globale Einstellungen wie Erkennungsdistanz und Verzögerungszeit. Mit ABD-Parametern kann das Gerät individuell an statische und bewegliche Ziele sowie verschiedene Umgebungen angepasst werden.

ABD ist der **einzige aktive Konfigurationspfad** auf fw v1.6.1. Er definiert:
- Welche Gates (Distanzbereiche) ausgewertet werden (`roiMin` / `roiMax`)
- Hohe und niedrige Energieschwellen pro Gate
- Verzögerungszeit vor der Anwesenheitsmeldung

## 1.1 ABD-Parameter konfigurieren (0x0007)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x0007` |
| **Antwort** | `0x0107` |
| **Sendedaten** | (2-Byte Parametername + 4-Byte Parameterwert) × N |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | Keine |

**Parameteradressen:**

| Adresse | Name | Beschreibung |
|---------|------|--------------|
| `0x0000` | roiMin | Minimales Erkennungsgate (verifiziert: Standard = 1) |
| `0x0001` | roiMax | Maximales Erkennungsgate (verifiziert: Standard = 8) |
| `0x0002` | delayTime | Haltezeit vor der Meldung |
| `0x0010–0x001F` | Hoher Schwellenwert Gate 0–15 | Bewegungsschwelle pro Gate |
| `0x0020–0x002F` | Niedriger Schwellenwert Gate 0–15 | Anwesenheitsschwelle pro Gate |

**Beispiel — roiMax auf Gate 8 setzen:**

```
Senden:   FD FC FB FA 0A 00 07 00 01 00 08 00 00 00 04 03 02 01
Empfang:  FD FC FB FA 04 00 07 01 00 00 04 03 02 01
```

---

## 1.2 ABD-Parameter lesen (0x0008)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x0008` |
| **Antwort** | `0x0108` |
| **Sendedaten** | (2-Byte Parametername) × N |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | (4-Byte Parameterwert) × N |

**Beispiel — roiMin lesen (einzelner Parameter):**

```
Senden:   FD FC FB FA 04 00 08 00 00 00 04 03 02 01
Empfang:  FD FC FB FA 08 00 08 01 00 00 01 00 00 00 04 03 02 01
Ergebnis: roiMin = 1
```

**Beispiel — roiMax lesen:**

```
Senden:   FD FC FB FA 04 00 08 00 01 00 04 03 02 01
Empfang:  FD FC FB FA 08 00 08 01 00 00 08 00 00 00 04 03 02 01
Ergebnis: roiMax = 8
```

> **Wichtig:** Immer einen Parameter pro Befehl lesen. Einzelparameter-Lesevorgänge sind zuverlässiger beim Testen.

---

## 1.3 Werksstandard-Schwellenwerte

```
Gate  0: Bewegung = 60000, Anwesenheit = 40000
Gate  1: Bewegung = 30000, Anwesenheit = 20000
Gate  2: Bewegung =   400, Anwesenheit =   200
Gate  3: Bewegung =   250, Anwesenheit =   200
Gate  4–15: Bewegung = 250, Anwesenheit = 150–100 (abnehmend)
```

Jedes Gate umfasst **70 cm**:

| Gate | Distanz |
|------|---------|
| 0 | 0 – 70 cm |
| 1 | 70 – 140 cm |
| 5 | 350 – 420 cm |
| 8 | 560 – 630 cm |
| 11 | 770 – 840 cm |
| 15 | 1050 – 1120 cm |

**Praktische Grenzen:**
- Wandmontage, Bewegung: ~8 m (Gate 11)
- Wandmontage, Mikrobewegung: ~6 m (Gate 8)
- Deckenmontage, Bewegung: ~5 m (Gate 7)
- Deckenmontage, Mikrobewegung: ~4 m (Gate 5)

---

# 2. Sensorinformationen

## 2.1 Firmware-Version lesen (0x0000)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x0000` |
| **Antwort** | `0x0100` |
| **Sendedaten** | Keine |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | 2-Byte Länge + Versionsstring |

**Beispiel:**

```
Senden:   FD FC FB FA 02 00 00 00 04 03 02 01
Empfang:  FD FC FB FA 0C 00 00 01 00 00 06 00 76 31 2E 36 2E 31 04 03 02 01
Ergebnis: v1.6.1
```

> **Hinweis:** Dieser Befehl antwortet sowohl im als auch außerhalb des Config-Modus.

---

## 2.2 Seriennummer lesen (0x0011)

> ⚠️ **Auf fw v1.6.1 nicht unterstützt** — keine Antwort. In offiziellen HLK-Unterlagen dokumentiert, aber nicht funktionsfähig.

---

# 3. Systemparameter

Systemparameter steuern den Betriebsmodus und das grundlegende Sensorverhalten.

## 3.1 Systemparameter konfigurieren (0x0012)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x0012` |
| **Antwort** | `0x0112` |
| **Sendedaten** | (2-Byte Parametername + 4-Byte Parameterwert) × N |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | Keine |

**Parameter:**

| Adresse | Name | Beschreibung |
|---------|------|--------------|
| `0x0000` | systemMode | Betriebsmodus (siehe Abschnitt 5) |
| `0x0001` | uploadSampleRate | Downsampling-Verhältnis |
| `0x0002` | debugMode | Debug-Flag |

**Beispiel — Energy-Modus setzen:**

```
Senden:   FD FC FB FA 08 00 12 00 00 00 04 00 00 00 04 03 02 01
Empfang:  FD FC FB FA 04 00 12 01 00 00 04 03 02 01
```

---

## 3.2 Systemparameter lesen (0x0013)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x0013` |
| **Antwort** | `0x0113` |
| **Sendedaten** | (2-Byte Parametername) × N |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | (4-Byte Parameterwert) × N |

---

# 4. Werkstest (Nicht für den Produktionseinsatz erforderlich)

Der Werkstest wird während der Produktion oder Qualitätskontrolle verwendet.

## 4.1 Werkstest-Modus - Eingabe (0x0024)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x0024` |
| **Antwort** | `0x0124` |
| **Sendedaten** | Keine |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | Siehe unten |

**Rückgabedaten:**
- `0x00–0x01`: Sub-Board-Modell (0 = reserviert)
- `0x02–0x03`: Kaskadierte Chip-Anzahl (1=einfach, 2=doppelt)
- `0x04–0x05`: Kanalanzahl
- `0x06–0x07`: Datentyp (0=1DFFT, 1=2DFFT, 2=2DFFT PEAK, 3=DSRAW)
- `0x08–0x09`: 1DFFT-Größe
- `0x0A–0x0B`: Chirps pro Frame
- `0x0C–0x0D`: Downsampling-Intervall

```
Empfang: FD FC FB FA 12 00 24 01 00 00 00 00 02 00 04 00 00 00 40 00 20 00 02 00 04 03 02 01
Ergebnis: Modell=0, Chips=2, Kanäle=4, Datentyp=0, 1DFFT=64, Chirps=32, Downsample=2
```

## 4.2 Werkstest-Modus - Ausgabe (0x0025)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x0025` |
| **Antwort** | `0x0125` |
| **Sendedaten** | Keine |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | Keine |

## 4.3 Werkstest - Ergebnisse senden (0x0026)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x0026` |
| **Antwort** | `0x0126` |
| **Sendedaten** | (2-Byte Adresse + 2-Byte Daten) × N |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | Keine |

---

# 5. Werksreset (0x0068)

Werksreset stellt alle Einstellungen auf Werksstandard zurück (Schwellenwerte, Gate-Konfiguration, Systemparameter, Betriebsmodus).

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x0068` |
| **Antwort** | `0x0168` |
| **Sendedaten** | Keine |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | Keine |

```
Senden:   FD FC FB FA 02 00 68 00 04 03 02 01
Empfang:  FD FC FB FA 04 00 68 01 00 00 04 03 02 01
```

---

# 6. Config-Modus

Der Config-Modus muss aktiv sein, bevor Konfigurationsbefehle gesendet werden. Der Sensor stoppt die Datenausgabe während des Config-Modus.

## 6.1 Config-Modus aktivieren (0x00FF)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x00FF` |
| **Antwort** | `0x01FF` |
| **Sendedaten** | 2-Byte Oberrechner-Version |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | 2-Byte Protokollversion + 2-Byte Puffergröße |

**Protokollversion:** 2  
**Puffergröße:** 1024 Bytes

```
Senden:   FD FC FB FA 04 00 FF 00 02 00 04 03 02 01
Empfang:  FD FC FB FA 08 00 FF 01 00 00 02 00 00 04 04 03 02 01
```

> **Kritisch:** Zweimal senden mit 100ms Flush dazwischen:
> 1. Aktivieren senden → auf Antwort warten
> 2. 100ms warten, alle eingehenden Bytes verwerfen
> 3. Erneut aktivieren senden → Config-Modus ist jetzt aktiv

---

## 6.2 Config-Modus deaktivieren (0x00FE)

| Eigenschaft | Wert |
|-------------|------|
| **Befehl** | `0x00FE` |
| **Antwort** | `0x01FE` |
| **Sendedaten** | Keine |
| **Rückgabestatus** | 2 Bytes (0=Erfolg) |
| **Rückgabedaten** | Keine |

```
Senden:   FD FC FB FA 02 00 FE 00 04 03 02 01
Empfang:  FD FC FB FA 04 00 FE 01 00 00 04 03 02 01
```

---

# 7. Benutzerdefinierte Befehle

**Bereich:** `0x0060–0x00A0`  
**Antwort:** `0x1060–0x10A0` (Anfrage + 0x1000)

---

# 8. Energy-Ausgabemodus

**Frame-Format:**

| Feld | Größe | Beschreibung |
|------|-------|--------------|
| Header | 4 Bytes | `F4 F3 F2 F1` |
| Datenlänge | 2 Bytes | `0x0023` (35 Bytes) |
| Anwesenheit/Bewegung | 1 Byte | 0=nichts, 1=Bewegung, 2=Anwesenheit |
| Distanz | 2 Bytes | Roher Sensorwert (mm), unverändert weitergegeben |
| Gate-Energien | 16 × 2 Bytes | Signalstärke pro Gate |
| Footer | 4 Bytes | `F8 F7 F6 F5` |

> **Hinweis:** Der Distanzwert wird exakt so weitergegeben, wie er vom Sensor gemeldet wird — ohne Interpolation oder Gate-Index-Zuordnung.

---

# 9. Debug-Ausgabemodus (RDMap)

**Frame-Format:**

| Feld | Größe | Beschreibung |
|------|-------|--------------|
| Header | 4 Bytes | `AA BF 10 14` |
| Daten | 20 × 16 × 4 Bytes = 1280 Bytes | Rohe Doppler-Daten |
| Trailer | 4 Bytes | `FD FC FB FA` |

- 20 Messzyklen × 16 Gates × 4 Bytes rohe Doppler-Energie
- Stream läuft **kontinuierlich** ohne Unterbrechung

---

# 10. Betriebsmodi — fw v1.6.1 Verifiziert

Betriebsmodi werden über Systemparameter `0x0000` (systemMode), Befehl `0x0012`, gesetzt.

| Wert | Name | Ausgabeformat | Status |
|------|------|---------------|--------|
| `0x0000` | Debug | Rohe Doppler-Frames (20×16×4 Bytes, kontinuierlich) | ✅ Verifiziert |
| `0x0001` | MTT | Simple-Text — ASCII ON/OFF/Range (CRLF) | ✅ Verifiziert — Alias von Simple |
| `0x0002` | VS | Simple-Text — ASCII ON/OFF/Range (CRLF) | ✅ Verifiziert — Alias von Simple |
| `0x0003` | GR | Simple-Text — ASCII ON/OFF/Range (CRLF) | ✅ Verifiziert — Alias von Simple |
| `0x0004` | Energy | Binäre Frames mit Gate-Energien + Distanz + Status | ✅ Verifiziert — Primärmodus |
| `0x0064` | Simple | Simple-Text — ASCII ON/OFF/Range (CRLF) | ✅ Verifiziert |

> **Hinweis:** MTT, VS und GR sind in offiziellen HLK-Unterlagen als Multi-Target / Vital-Sign / Gruppen-Modi dokumentiert, geben aber auf fw v1.6.1 alle identisches Simple-Text-Format aus. Es handelt sich wahrscheinlich um LD2450-Features, die in dieser Firmware nicht implementiert sind. Nur **Debug** und **Energy** sind funktional eigenständige Modi.

**Simple-Text-Format:**
```
ON\r\n
Range 493\r\n
OFF\r\n
```

---

# 11. Befehlsübersicht

| Funktion | Befehl | Antwort | Sendedaten | Rückgabedaten |
|----------|--------|---------|------------|---------------|
| Firmware-Version lesen | `0x0000` | `0x0100` | Keine | Länge + Versionsstring |
| Register schreiben *(obsolet)* | `0x0001` | `0x0101` | Chip-Adr. + (Adr. + Daten) × N | Keine |
| Register lesen *(obsolet)* | `0x0002` | `0x0102` | Chip-Adr. + (Adr.) × N | (Daten) × N |
| ABD-Parameter konfigurieren | `0x0007` | `0x0107` | (Param + Wert) × N | Keine |
| ABD-Parameter lesen | `0x0008` | `0x0108` | (Param) × N | (Wert) × N |
| Seriennummer lesen *(nicht unterstützt)* | `0x0011` | `0x0111` | Keine | Modul-ID + Seriennummer |
| Systemparameter konfigurieren | `0x0012` | `0x0112` | (Param + Wert) × N | Keine |
| Systemparameter lesen | `0x0013` | `0x0113` | (Param) × N | (Wert) × N |
| Werkstest - Eingabe | `0x0024` | `0x0124` | Keine | Testdaten |
| Werkstest - Ausgabe | `0x0025` | `0x0125` | Keine | Keine |
| Werkstest - Ergebnisse senden | `0x0026` | `0x0126` | (Adr. + Daten) × N | Keine |
| Werksreset | `0x0068` | `0x0168` | Keine | Keine |
| Config-Modus aktivieren | `0x00FF` | `0x01FF` | Oberrechner-Version | Protokollversion + Puffergröße |
| Config-Modus deaktivieren | `0x00FE` | `0x01FE` | Keine | Keine |
| Benutzerdefinierte Befehle | `0x0060–0x00A0` | `0x1060–0x10A0` | — | — |

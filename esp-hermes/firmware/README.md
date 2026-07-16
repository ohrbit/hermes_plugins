# ESP-Hermes Firmware — Anleitung für ESP-IDF-Neulinge

Diese Anleitung geht davon aus, dass du **noch nie mit Espressif-Tools
gearbeitet hast**. Wenn du schon weißt was ESP-IDF ist, spring zu
[„Schritt 4: Builden"](#schritt-4-builden-und-flashen).

---

## Was ist ESP-IDF und warum brauchst du es?

**ESP-IDF** (Espressif IoT Development Framework) ist das offizielle
Werkzeug-Set von Espressif, um Programme auf den ESP32-Chip zu flashen
(aufzuspielen). Dein M5Stack Stick S3 hat einen **ESP32-S3**-Chip drin — das
ist im Grunde ein kleiner Computer mit WiFi. Um unseren `esp-hermes`-Code
(geschrieben in C) auf den Chip zu bekommen, brauchst du ESP-IDF.

Es besteht aus:
- **Toolchain** — übersetzt C-Code in Maschinencode für den Chip
- **Build-System** (`idf.py`) — steuert Kompilieren + Flashen
- **ESP-IDF-Bibliotheken** — vorgefertigter Code für WiFi, I2C, LCD, etc.

> ⚠️ **Status:** Firmware ist geschrieben + an Stick-S3-Pins verdrahtet, aber
> noch **nicht auf echter Hardware getestet**. Erster Build wird vermutlich
> noch Fehler zeigen — das ist normal. Poste sie, wir fixen sie.

---

## Schritt 1: ESP-IDF installieren

### Windows / macOS / Linux

1. Downloade den **ESP-IDF Installer** von der offiziellen Seite:
   👉 https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/get-started/index.html
   (Da du v6 installiert hast: das ist auch OK, v5→v6 ist meist kompatibel.
    Falls Build-Fehler kommen, sag Bescheid.)

2. **Windows:** Der Installer heißt `esp-idf-tools-setup-*.exe`. Doppelklick →
   „Next" → wähle eine ESP-IDF-Version (v5.2+ oder v6) → Installer lädt
   Toolchain + Python + Git automatisch runter.

3. **macOS / Linux:** folge der „Manual Installation" auf der obigen Seite,
   oder nutze den Installer. Am Ende brauchst du einen Terminal-Befehl, der
   das Environment lädt (siehe Schritt 2).

4. Nach der Installation: **Starte eine neue Terminal/Kommandozeile** (wichtig!).
   Auf Windows macht der Installer eine Verknüpfung „ESP-IDF Command Prompt" —
   **nutze die**, nicht die normale cmd.exe.

---

## Schritt 2: ESP-IDF-Umgebung aktivieren

Jedes Mal wenn du baust, muss das Environment geladen sein.

**Windows (ESP-IDF Command Prompt):**
```bat
# ist schon geladen wenn du die ESP-IDF-Verknüpfung nutzt
# sonst:
%IDF_PATH%\export.bat
```

**macOS / Linux (Terminal):**
```bash
. $HOME/esp/esp-idf/export.sh
# oder wo du es installiert hast, z.B.:
. ~/esp-idf/export.sh
```

Test: gib ein `idf.py --version`. Wenn eine Versionsnummer kommt → fertig.
Wenn „command not found" → Environment nicht geladen (Schritt 2 wiederholen).

---

## Schritt 3: Firmware-Code auf deinen PC holen

Du brauchst den Quellcode lokal. Zwei Wege:

**A. Über Git (empfohlen):**
```bash
git clone https://github.com/ohrbit/hermes_plugins
cd hermes_plugins/esp-hermes/firmware
```

**B. Als ZIP:**
1. Gehe auf https://github.com/ohrbit/hermes_plugins
2. Klicke grünen „Code" → „Download ZIP"
3. Entpacke es, gehe in `esp-hermes/firmware/`

---

## Schritt 4: Builden und Flashen

Jetzt der eigentliche Flash-Vorgang. Ablauf:
```
set-target → build → flash → monitor
```

### 4.1 Ziel-Chip setzen
```bash
idf.py set-target esp32s3
```
(Sagt ESP-IDF: baue für ESP32-S3, nicht für einen anderen Chip.)

### 4.2 Code kompilieren
```bash
idf.py build
```
Das dauert beim ersten Mal **2–5 Minuten** (ESP-IDF kompiliert alle
Bibliotheken). Wenn am Ende `Project build complete` steht → erfolgreich.
Bei Fehlern: ganze Fehlermeldung kopieren + an Hermes schicken.

### 4.3 Stick S3 in Download-Modus versetzen
Bevor du flashst, muss der Stick den „Download-Modus" kennen:
1. **USB-Kabel** an PC anschließen (nicht nur Strom!).
2. **Reset-Knopf** (seitlich am Stick) **gedrückt halten**.
3. Kurz warten bis die **grüne LED blinkt** → loslassen.
4. Jetzt ist der Stick im Download-Modus.

> Hinweis: Manche Sticks erkennen den Modus automatisch beim Flashen. Falls
> `idf.py flash` mit „Failed to connect" abbricht → Reset-Knopf kurz drücken
> und es nochmal versuchen.

### 4.4 Flashen + Monitor starten
```bash
idf.py flash monitor
```
- `flash` lädt den kompilierten Code auf den Chip
- `monitor` öffnet eine „Serial Console" — du siehst was der Chip per
  USB ausgibt (Logs, Fehler, Debug)

**Erfolg:** Du siehst Boot-Logs + `ESP-Hermes ready`.

**Abbruch Monitor:** `Ctrl + ]` (Strg + schließende Klammer).

---

## Schritt 5: Erstkonfiguration (WiFi + Device-Token)

Beim ersten Start braucht der Stick:
- **WiFi-SSID + Passwort** (um zum Hermes-Gateway zu kommen)
- **Device-Token** (damit Gateway weiß: das ist dein Device)
- **Gateway-Host** (URL deines Hermes-Servers)

Diese Werte kommen in den **NVS-Speicher** (persistent auf dem Chip).
Mechanismus ist noch nicht fertig (siehe TODO unten) — aktuell musst du sie
im Code (`nvs_config.c`) hart eintragen oder über Serial provisionieren.

> Sobald der NVS-Flow steht, reicht: `hermes config set esp_hermes.devices.stick-s3.wifi "SSID:password"`

---

## Pin-Belegung (Stick S3, verifiziert)

| Peripheral | Pins |
|---|---|
| LCD (ST7789P3 135×240) | MOSI=G39, SCK=G40, RS=G45, CS=G41, RST=G21, BL=G38 |
| IMU (BMI270 0x68) | SCL=G48, SDA=G47 |
| Audio (ES8311 0x18) | MCLK=G18, DOUT=G14, BCLK=G17, LRCK=G15, DIN=G16 |
| Buttons | KEY1=G11 (PTT), KEY2=G12 (Mode) |
| Port.A | G9, G10 |

---

## Bekannte Probleme (Troubleshooting)

| Symptom | Lösung |
|---|---|
| `idf.py: command not found` | Environment nicht geladen → Schritt 2 |
| `Failed to connect` beim Flash | Reset-Knopf im Download-Modus (Schritt 4.3) |
| `Permission denied /dev/ttyUSB0` (Linux) | `sudo usermod -aG dialout $USER`, neu einloggen |
| Chip nicht erkannt (Windows) | Treiber installieren: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers |
| Build schlägt fehl | Ganze Fehlermeldung an Hermes schicken |

---

## TODO (echte Hardware nötig)

- [ ] Erster ESP-IDF-Build (strukturell geprüft, keine Toolchain auf Build-Host)
- [ ] ES8311 I2S-Init + Mic
- [ ] ST7789P3 LCD + petdex-Render
- [ ] BMI270 Reads + Gesten
- [ ] WS-Connect + Capabilities-Handshake
- [ ] TTS-Downlink
- [ ] NVS-Provisioning-Flow
- [ ] Opus vs PCM (spec §11) — aktuell PCM-Stub

Voller Plan: `../references/implementation-spec.md` §5.

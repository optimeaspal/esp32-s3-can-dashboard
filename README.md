# ESP32-S3 CAN Dashboard

Grafisches, **frei konfigurierbares** CAN-Bus-Dashboard für den
**Waveshare ESP32-S3-Touch-LCD-7** (7″ IPS, 800×480, kapazitiver Touch).
LVGL-Widgets (Gauge, Arc, Chart, Bar, LED, Label) visualisieren CAN-Signale in
Echtzeit. Layout, Signal-Mapping und Farben werden über eine `dashboard.json`
auf der SD-Karte definiert – **ohne Neukompilieren**. Bearbeitet wird die
Konfiguration komfortabel über einen mitgelieferten **Web-Editor**, die fertige
Datei kann per **WLAN** auf das Gerät hochgeladen werden.

---

## Features

- **JSON-konfiguriertes Dashboard** – Signale, Seiten und Widgets in
  `dashboard.json`; keine Code-Änderung, kein Flashen nötig.
- **6 Widget-Typen** – `gauge`, `arc`, `chart`, `bar`, `led`, `label`, pixelgenau
  platzierbar auf einem 800×480-Raster.
- **Mehrere Seiten** mit horizontaler Wisch-Navigation und Seiten-Indikator (Punkte).
- **Echter CAN-Bus** (TWAI/TJA1051, 25 kBit/s … 1 MBit/s) **oder Simulator** –
  per Schalter umstellbar, der Simulator erzeugt synthetische Werte ohne Bus.
- **Flexible Signal-Dekodierung** – Byte-Offset/-Länge, Little-/Big-Endian,
  signed/unsigned Integer oder IEEE-754-Float, Scale + Offset, Einheit.
- **Stale-Erkennung** – Widgets werden ausgegraut, wenn ein Signal länger als sein
  Timeout kein Update bekommt.
- **Warnschwellen** – pro Widget eine Schwelle mit eigener Warnfarbe.
- **Web-Editor** – Konfiguration visuell bearbeiten (Canvas mit Drag & Resize,
  gerätegetreue Widget-Vorschau, Farbwähler). Läuft auf dem Gerät **oder offline
  im Browser**.
- **Drahtloser Upload** – Konfiguration via Browser hochladen
  (`http://dashboard.local`); das Gerät **validiert vor dem Ersetzen** und startet
  bei Erfolg neu.
- **Settings-/Info-Screen** – ein Zahnrad-Icon (Farbe zeigt den WLAN-Status)
  öffnet eine read-only Diagnoseseite: WLAN/IP, Geräte- & Firmware-Info, SD-Status
  und ein **Live-CAN-RX-Monitor** (empfangene Frames je CAN-ID mit Datenbytes und
  Rate) – ideal zur Inbetriebnahme direkt am Gerät, ohne seriellen Monitor.
- **Native Unit-Tests** – Dekodierungs-, Parser- und Editor-Logik auf dem PC
  testbar (kein Board nötig).

---

## Hardware

| Komponente | Detail |
|---|---|
| MCU | ESP32-S3, Dual-Core 240 MHz |
| Display | 7″ IPS 800×480, ST7701, RGB-Interface |
| Touch | GT911, kapazitiv, 5-Punkt |
| Flash | 8 MB QIO |
| PSRAM | 8 MB Octal |
| CAN-Transceiver | TJA1051, onboard |
| IO-Expander | CH422G (I²C, steuert u.a. SD-CS und USB/CAN-Routing) |
| SD/TF-Karte | Pflicht – enthält `dashboard.json` (und optional `wifi.json`, `www/`) |

### Pinbelegung

| Signal | GPIO |
|---|---|
| CAN TX | GPIO 20 |
| CAN RX | GPIO 19 |
| I²C SDA (CH422G/GT911) | GPIO 8 |
| I²C SCL (CH422G/GT911) | GPIO 9 |
| SD SPI MOSI / MISO / CLK | GPIO 11 / 13 / 12 |
| SD Chip-Select | über CH422G-Pin 4 (nicht direkter GPIO) |

> **Hinweis:** Der CH422G-Expander schaltet bei CAN-Init das USB/CAN-Routing um,
> sodass GPIO 19/20 zum TJA1051-Transceiver geführt werden.

### CAN-Bus anschließen

- Board-seitig: PH2.0-2-Pin (CAN-H, CAN-L) + separater GND-Draht an Board-GND.
- **Terminierung:** 120 Ω an beiden Bus-Enden – prüfen, ob am Board bereits vorhanden.
- Bitrate muss zum Bus passen (`CONFIG_CAN_BITRATE_KBPS`, Standard 500 kBit/s).

---

## Schnellstart

### 1. Voraussetzungen

- VS Code + **PlatformIO**-Extension (oder PlatformIO CLI)
- Git
- Framework: **ESP-IDF** (≥ 5.1, wird von PlatformIO automatisch bereitgestellt)

### 2. Repository klonen

```bash
git clone git@github.com:optimeaspal/esp32-s3-can-dashboard.git
cd esp32-s3-can-dashboard
```

### 3. Bauen, flashen, beobachten

Board per USB-C anschließen (ggf. BOOT halten während RESET für den Boot-Modus):

```bash
pio run -e esp32-s3-touch-lcd-7              # bauen
pio run -e esp32-s3-touch-lcd-7 -t upload    # flashen
pio device monitor                           # serielle Ausgabe (115200 Baud)
```

Beim ersten Build lädt der ESP-IDF Component Manager LVGL und den GT911-Treiber
automatisch nach.

### 4. SD-Karte vorbereiten

Die SD-Karte ist **erforderlich** – ohne `dashboard.json` zeigt das Gerät einen
Fehlerbildschirm. FAT32 formatieren und folgende Struktur anlegen:

```
SD-Karte (Root)
├── dashboard.json        ← Pflicht: Dashboard-Konfiguration
├── wifi.json             ← optional: WLAN-Zugangsdaten für den Upload
└── www/                  ← optional: Web-Editor-Assets (für Bedienung am Gerät)
    ├── index.html
    ├── style.css
    ├── editor-core.js
    └── app.js
```

Als Startpunkt die Beispiele aus `examples/` kopieren:

- `examples/dashboard.json` – vollständiges 2-Seiten-Beispiel (oder
  `dashboard.minimal.json` als Minimalvariante)
- `examples/wifi.json` – Vorlage für die WLAN-Zugangsdaten (SSID/Passwort eintragen)
- `examples/www/*` – die vier Editor-Dateien nach `www/` kopieren

> Lange Dateinamen (LFN) sind in der Firmware aktiviert – `dashboard.json` &
> `dashboard.json.tmp` funktionieren also trotz 8.3-Default.

---

## Bedienung

- **Seiten wechseln:** horizontal über das Display wischen. Die Punkte am unteren
  Rand zeigen die aktive Seite (nur sichtbar ab 2 Seiten).
- **Live-Werte:** Widgets aktualisieren sich automatisch aus den CAN-Daten
  (bzw. vom Simulator).
- **Stale/Verbindungsverlust:** Bleibt ein Signal länger als sein `stale_ms`-Timeout
  ohne Update, wird das zugehörige Widget ausgegraut.
- **Warnung:** Überschreitet ein Wert die `warning_threshold` eines Widgets,
  wechselt es auf seine Warnfarbe.
- **Konfiguration ändern:** entweder SD-Karte mit neuer `dashboard.json` umstecken
  **oder** drahtlos über den Web-Editor hochladen (siehe unten). Nach erfolgreichem
  Upload startet das Gerät automatisch mit der neuen Konfiguration neu.
- **Settings-/Info-Screen:** Das **Zahnrad-Icon** oben rechts (Farbe = WLAN-Status:
  grün verbunden / gelb verbindet / grau inaktiv / rot fehlgeschlagen) öffnet die
  Diagnoseseite; **„‹ Zurueck"** führt zum Dashboard zurück (siehe unten).

---

## Settings-/Info-Screen

Read-only Diagnoseseite zur Inbetriebnahme direkt am Gerät – erreichbar über das
Zahnrad-Icon oben rechts (über allen Dashboard-Seiten sichtbar). Zweispaltiges
Layout:

- **WLAN / Netzwerk** – SSID, Verbindungsstatus, IP-Adresse, mDNS-Hostname
  (`dashboard.local`), Signalstärke (RSSI). Bei deaktiviertem WLAN: „WLAN deaktiviert".
- **Gerät / Firmware** – Firmware-Version, Build-Datum, Uptime, freier Heap + PSRAM,
  Anzahl geladener Seiten.
- **SD-Karte** – erkannt ja/nein, Vorhandensein von `dashboard.json` und `wifi.json`.
- **CAN-RX-Monitor** – live empfangene Frames **gruppiert pro CAN-ID** (sortiert),
  je Zeile `ID | letzte Datenbytes (hex) | Frames/s`; Kopfzeile mit ID-Anzahl und
  Gesamt-Framerate. Aktualisierung ~4×/s, nur solange der Screen offen ist.

> v1 ist bewusst **rein informativ** (keine Eingaben). Der Screen ist als
> erweiterbarer Rahmen gebaut, einfache Aktionen können später ergänzt werden.

---

## Web-Editor

Visuelles Werkzeug zum Bearbeiten der `dashboard.json` (Signale, Seiten, Widgets)
mit gerätegetreuer Vorschau. Reine Vanilla-HTML/CSS/JS-Anwendung – keine Frameworks,
keine CDNs. Quellcode unter [`examples/www/`](examples/www/), Details in
[`examples/www/README.md`](examples/www/README.md).

**Bedienung im Layout-Tab:** Widgets per Maus ziehen und am Eck-Anfasser
skalieren; ein ausgewähltes Widget zusätzlich mit den **Pfeiltasten** (1 px,
mit **Shift** 10 px) verschieben, **Entf** löschen, **Esc** abwählen. Der
**Vorschau-Schieberegler** treibt den Beispielwert aller Widgets von 0–100 %
durch (zum Prüfen von Warnschwelle und Farben). Seiten per **Doppelklick**
umbenennen und über den 🗑-Button am aktiven Tab löschen.

### Variante A – am Gerät (drahtlos)

1. `examples/wifi.json` anpassen (SSID + Passwort) und als `wifi.json` in den
   SD-Root legen. Beim Start verbindet sich das Gerät im Hintergrund (Anzeige
   bleibt < 3 s blockiert) und startet einen HTTP-Server.
2. Editor im Browser öffnen: `http://dashboard.local` (mDNS) oder die IP aus dem
   seriellen Log.
3. Konfiguration bearbeiten → **„Speichern & Übernehmen"**. Das Gerät validiert die
   Datei, ersetzt sie atomar und startet neu.

`wifi.json`-Format (mehrere Netze werden der Reihe nach probiert):

```json
{
  "version": "1.0",
  "hostname": "dashboard",
  "networks": [
    { "ssid": "MEIN-WLAN", "password": "GEHEIM" }
  ]
}
```

### Variante B – offline am PC (ohne Gerät)

1. `examples/www/start-editor-offline.bat` doppelklicken (startet einen lokalen
   HTTP-Server, benötigt Python) – oder `index.html` direkt im Browser öffnen.
2. Neue Konfiguration erstellen oder vorhandene `dashboard.json` importieren,
   bearbeiten und wieder **exportieren**.
3. Exportierte Datei auf die SD-Karte kopieren oder über Variante A hochladen.
   („Speichern & Übernehmen" ist offline deaktiviert.)

---

## Konfigurationsformat (`dashboard.json`)

Kurzüberblick – die vollständige Feld-Referenz steht im
[JSON-Schema-Vertrag](specs/001-json-config-dashboard/contracts/dashboard-json-schema.md).

```json
{
  "version": "1.0",
  "signals": [ /* 1–32 Signale */ ],
  "pages":   [ /* 1–8 Seiten mit je 0–16 Widgets */ ],
  "tx_commands": []
}
```

**Signal** (Auswahl): `name` (eindeutig), `can_id` (`"0x102"` oder dezimal),
`byte_offset`, `byte_length` (1/2/4), `endianness` (`little`/`big`), optional
`signed`, `float`, `scale`, `offset`, `unit`, `min`/`max`, `stale_ms`, `simulated`.

**Widget** (Auswahl): `type` (`gauge`/`arc`/`chart`/`bar`/`led`/`label`), `x`, `y`,
`width`, `height`, `signal` (Referenz auf einen Signalnamen), optional `title`,
`normal_color`, `warning_color`, `warning_threshold`, `background_color`
(Farben als `"#RRGGBB"`).

Validierungsregeln (u.a.): Signalnamen eindeutig, `min < max`,
`byte_offset + byte_length ≤ 8`, jede Widget-`signal`-Referenz muss existieren.
Bei Fehlern zeigt das Gerät eine konkrete Meldung und übernimmt die alte
Konfiguration nicht.

---

## Konfiguration der Firmware (Build-Zeit)

Über `pio run -t menuconfig` oder `sdkconfig.defaults`:

| Parameter | Kconfig | Standard |
|---|---|---|
| CAN-Bitrate (kBit/s) | `CONFIG_CAN_BITRATE_KBPS` | 500 |
| CAN TX / RX GPIO | `CONFIG_EXAMPLE_TX_GPIO_NUM` / `_RX_` | 20 / 19 |
| Stale-Timeout (ms) | `CONFIG_CAN_SIGNAL_STALE_MS` | 2000 |
| RX Queue-Länge | `CONFIG_CAN_RX_QUEUE_LEN` | 32 |
| **CAN-Simulator** | `CONFIG_CAN_SIMULATOR_ENABLE` | `y` |
| Dashboard-Pfad | `CONFIG_DASHBOARD_JSON_PATH` | `/sdcard/dashboard.json` |
| WLAN-Upload | `CONFIG_DASHBOARD_WIFI_ENABLE` | `y` |
| WLAN-Pfad | `CONFIG_DASHBOARD_WIFI_PATH` | `/sdcard/wifi.json` |
| HTTP-Port | `CONFIG_DASHBOARD_HTTP_PORT` | 80 |

> **Echten CAN-Bus nutzen:** `CONFIG_CAN_SIMULATOR_ENABLE=n` setzen. Dann liefern
> nur reale CAN-Frames Werte; Signale mit `"simulated": true` bleiben ohne Bus stale.
> Mit `=y` erzeugt der Simulator für alle `simulated`-Signale synthetische Verläufe.

> **Speicher-Hinweis:** Der interne DMA-RAM ist durch RGB-Framebuffer + LVGL sehr
> knapp. `sdkconfig.defaults` lagert daher WiFi/LWIP-Puffer und große statische
> Puffer ins PSRAM aus – diese Einstellungen nicht ohne Grund entfernen.

---

## Architektur

```
app_main()
  ├─ waveshare_rgb_lcd_init()  ← ST7701-RGB + GT911-Touch + LVGL
  ├─ waveshare_sd_port_init()  ← SD mounten, dashboard.json lesen
  ├─ config_loader_parse()     ← JSON → dashboard_config_t (validiert)
  ├─ dashboard_create()        ← LVGL-Seiten (Tileview) + Widgets aufbauen
  ├─ status_icon_create()      ← Zahnrad-Icon (öffnet Settings-/Info-Screen)
  ├─ can_dispatcher_start()    ← CAN-Task (Core 0): twai_receive → decode → Queue
  │     ├─ (alternativ) can_simulator: synthetische Werte
  │     └─ can_monitor: Pro-ID-Statistik für den CAN-RX-Monitor
  └─ network_task()            ← optional: WLAN verbinden + HTTP-Upload-Server
```

Widget-Updates laufen im LVGL-Timer-Task (Core 1) und konsumieren dekodierte
Werte aus einer Queue; die Stale-Prüfung graut Widgets bei Timeout aus. Der
Settings-/Info-Screen pollt per LVGL-Timer den WLAN-Status (Icon-Farbe) und einen
thread-sicheren Snapshot der `can_monitor`-Tabelle (CAN-RX-Monitor).

---

## Projektstruktur

```
├── src/
│   ├── main.c                  ← Initialisierungssequenz, Tasks
│   ├── Kconfig.projbuild       ← menuconfig-Optionen
│   ├── app/                    ← hardware-unabhängige, testbare Logik
│   │   ├── can_signal.*        ← Signal-Dekodierung
│   │   ├── can_dispatcher.*    ← Empfang + Verteilung
│   │   ├── can_monitor.*       ← Pro-ID-Statistik (CAN-RX-Monitor)
│   │   ├── can_simulator.*     ← synthetische Werte
│   │   ├── config_loader.*     ← dashboard.json parsen/validieren
│   │   ├── config_types.h      ← Datenmodell
│   │   └── wifi_credentials.*  ← wifi.json parsen
│   ├── hal/                    ← ESP-IDF-/Board-Anbindung
│   │   ├── lvgl_port.*  waveshare_rgb_lcd_port.*  waveshare_sd_port.*
│   │   ├── waveshare_twai_port.*  waveshare_wifi_port.*
│   │   └── web_server.*        ← HTTP-Server (Editor-Assets + /api/config)
│   └── ui/                     ← LVGL-Dashboard
│       ├── dashboard.*  nav_indicator.*  widget_registry.*
│       ├── status_icon.*       ← Zahnrad-Icon (WLAN-Statusfarbe)
│       ├── settings_screen.*   ← Settings-/Info-Screen (Diagnose, CAN-RX-Monitor)
│       └── widgets/            ← gauge/arc/chart/bar/led/label
├── examples/
│   ├── dashboard.json  dashboard.minimal.json  wifi.json
│   └── www/                    ← Web-Editor (auf SD nach /www/ kopieren)
├── test/                       ← native Unit-Tests + Fixtures
├── specs/                      ← Spezifikationen & JSON-Schema-Vertrag
├── platformio.ini  sdkconfig.defaults  default_8MB.csv
```

---

## Tests

Native Tests laufen ohne Board auf dem PC (Unity bzw. Node):

```bash
pio test -e native                          # C-Logik: Dekodierung, Parser, Validierung
node --test test/www/editor-core.test.cjs   # Editor-Kernlogik (DOM-frei)
```

---

## CAN ohne Fahrzeug testen

Test-Frames z.B. mit einem USB-CAN-Adapter einspeisen. Die CAN-IDs und der
Wertebereich richten sich nach der jeweils geladenen `dashboard.json`. Für das
Beispiel `examples/dashboard.json` (Simulator aus) etwa:

| Signal | CAN-ID | Beispiel-Frame (Little-Endian) |
|---|---|---|
| RPM (gauge) | 0x102 | `B8 0B 00 00 00 00 00 00` (= 3000) |
| Kraftstoff (arc/bar) | 0x103 | `80 …` (raw 128 × 0.392 ≈ 50 %) |

Alternativ einfach den Simulator aktiviert lassen (`CONFIG_CAN_SIMULATOR_ENABLE=y`),
dann werden alle `simulated`-Signale ohne Bus animiert.

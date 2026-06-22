# Feature 005 – Settings-/Info-Screen

**Datum:** 2026-06-22
**Status:** Design abgenommen, bereit für Plan

## Ziel & Motivation

Ein Zahnrad-Icon im Dashboard führt zu einem eigenen **Settings-/Info-Screen**.
Zweck ist primär die **Inbetriebnahme direkt am Gerät**: Man soll WLAN-Status,
IP-Adresse und die live empfangenen CAN-Frames sehen, ohne am seriellen Monitor
hängen zu müssen.

v1 ist bewusst **read-only** (reine Anzeige), der Screen ist aber als
erweiterbarer Rahmen gebaut, damit einfache Eingaben (z. B. Reboot,
Display-Helligkeit) später leicht andocken können.

## Scope

**In v1 enthalten:**
- Status-farbiges Zahnrad-Icon, dauerhaft über allen Dashboard-Seiten sichtbar
- Eigener Settings-Screen mit „Zurück"-Navigation
- Vier read-only Info-Bereiche: WLAN/Netzwerk, Gerät/Firmware, SD-Karte,
  CAN-RX-Monitor (gruppiert pro ID)

**Bewusst NICHT in v1 (YAGNI):**
- Keine editierbaren Einstellungen / keine Texteingabe
- Kein durchlaufendes CAN-Log (Variante „rollendes Log" / umschaltbar später)
- Keine CAN-TX-Funktion
- Keine WLAN-Konfiguration über den Screen

## 1. Einstieg & Navigation

- **Status-farbiges Zahnrad-Icon**, dauerhaft oben rechts über allen
  Dashboard-Seiten. Es liegt auf `lv_layer_top()` und ist damit unabhängig von
  der JSON-Dashboard-Konfiguration und über alle Tiles hinweg sichtbar.
- Die Farbe richtet sich nach `wifi_port_status_t`:
  - grün = `WIFI_PORT_CONNECTED`
  - gelb = `WIFI_PORT_CONNECTING`
  - grau = `WIFI_PORT_IDLE`
  - rot = `WIFI_PORT_FAILED`
  - Damit deckt das Icon zugleich die ursprüngliche „WLAN-Status-Icon"-Idee ab.
- **Tap** öffnet den Settings-Screen als **eigenen LVGL-Screen**
  (`lv_screen_load_anim`, Slide-Übergang). Eine Kopfzeile mit **„‹ Zurück"**
  kehrt zum Dashboard-Screen zurück.
- Der Settings-Screen ist **kein** Swipe-Tile im `lv_tileview` – er vermischt
  sich nicht mit den frei konfigurierbaren Nutzer-Seiten.

## 2. Layout (Zwei-Spalten, 1024×600)

```
┌──────────────────────────────────────────────┐
│ ‹ Zurück    Einstellungen & Info              │  Kopfzeile
├───────────────────┬──────────────────────────┤
│ WLAN/Netzwerk     │                          │
├───────────────────┤   CAN-RX-Monitor         │
│ Gerät/Firmware    │   (gruppiert pro ID,     │
├───────────────────┤    volle Höhe)           │
│ SD-Karte          │                          │
└───────────────────┴──────────────────────────┘
   linke Spalte ~42 %        rechte Spalte
```

- **Kopfzeile:** „‹ Zurück"-Button + Titel „Einstellungen & Info".
- **Linke Spalte (~42 %):** drei gestapelte read-only Info-Karten.
- **Rechte Spalte:** CAN-RX-Monitor über die volle Höhe (mehr Zeilen sichtbar).

## 3. Info-Karten (read-only)

- **WLAN/Netzwerk:** SSID, Verbindungsstatus, IP-Adresse, mDNS-Hostname
  (`dashboard.local`), Signalstärke (RSSI) falls verfügbar.
  Quelle: `waveshare_wifi_port_get_status()` / `waveshare_wifi_port_get_ip()`.
- **Gerät/Firmware:** FW-Version, Build-Datum (`__DATE__`), Uptime
  (`esp_timer`), freier Heap + freies PSRAM (`heap_caps`), geladene Config
  (Dateiname + Seitenzahl aus `dashboard_config_t`).
- **SD-Karte:** erkannt ja/nein, Kapazität soweit über `waveshare_sd_port`
  verfügbar, vorhandene relevante Dateien (`dashboard.json`, `wifi.json`).

## 4. CAN-RX-Monitor (gruppiert pro ID)

- **Eine Zeile je CAN-ID**, sortiert nach ID:
  `ID | letzte Datenbytes (hex) | Frames/s`.
  Kopfzeile zeigt Gesamt-fps und Anzahl aktiver IDs.
- Begründung gegen ein durchlaufendes Log: Bei hoher Buslast (>100 fps) ist ein
  rollendes Log unlesbar; die gruppierte Tabelle bleibt ruhig und beantwortet
  die Inbetriebnahme-Frage „kommt etwas rein, von welcher ID, wie oft?".
- **Datenpfad:** Der `can_dispatcher` führt zusätzlich eine kleine, feste
  **Monitor-Tabelle** (pro ID: letzte Datenbytes, DLC, rollender Zähler → fps,
  letzter Zeitstempel) und bietet einen thread-sicheren Getter
  (z. B. `can_dispatcher_get_monitor(...)`). Dies orientiert sich am bereits
  vorhandenen `CAN_DEBUG_STATS`-Mechanismus, ist aber dauerhaft aktiv und über
  einen Getter abrufbar.
- Die UI pollt diese Tabelle (~4 Hz) **nur wenn der Settings-Screen offen ist**.
  Keine zusätzliche FreeRTOS-Queue, kein Einfluss auf den bestehenden
  Dashboard-Signalpfad.
- Frames, deren ID über ein Timeout nicht mehr aktualisiert wird, können
  ausgegraut oder aus der Tabelle entfernt werden.

## 5. Architektur / neue Bausteine

- **`ui/settings_screen.{c,h}`** – baut den Screen samt Kopfzeile, Info-Karten
  und CAN-Tabelle auf; `settings_screen_tick()` aktualisiert die Live-Werte.
- **`ui/status_icon.{c,h}`** – das Zahnrad-Icon auf `lv_layer_top()`, färbt sich
  periodisch nach WLAN-Status, Klick-Handler öffnet den Settings-Screen.
- **`can_dispatcher`** – erweitert um die Monitor-Tabelle und einen
  thread-sicheren Getter.
- Aktualisierung aller Live-Werte erfolgt aus dem LVGL-Task innerhalb des
  LVGL-Mutex (`lvgl_port_lock/unlock`), analog zu `dashboard_tick()`.

## 6. Hinweise / Watchlist

- **Interner RAM ist knapp** (siehe Memory): Monitor-Tabelle klein und fest
  dimensionieren; große Puffer ggf. ins PSRAM.
- **LVGL `lv_label_set_text_fmt` kann kein `%f`** (siehe Memory): für
  Float-Werte `snprintf` + `lv_label_set_text` verwenden.
- Hex-Formatierung der CAN-Bytes über `snprintf`, nicht über `%f`-artige Pfade.

## Offene Punkte für den Plan

- Genaue feste Größe der Monitor-Tabelle (max. Anzahl IDs).
- Quelle/Format der FW-Version (Define vs. `git describe`).
- Verfügbarkeit von RSSI und SD-Kapazität in den vorhandenen HAL-Ports prüfen;
  fehlende Werte sauber als „n/a" anzeigen.

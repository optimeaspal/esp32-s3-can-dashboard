# Design: Drahtloser Upload der dashboard.json über WLAN

**Datum:** 2026-06-19
**Status:** Freigegeben (Brainstorming abgeschlossen)
**Branch (Empfehlung):** eigenes Feature-Branch, z. B. `002-wlan-config-upload`
**Bezug:** baut auf `001-json-config-dashboard` auf (SD-basierte `dashboard.json`)

## Problem

Die `dashboard.json` muss heute manuell auf der SD-Karte aktualisiert werden:
Karte aus dem Display entnehmen, in den PC stecken, Datei kopieren, zurückstecken.
Das ist umständlich. Das Gerät kann WLAN — der Upload soll drahtlos erfolgen.

## Ziel (v1)

Über ein lokales WLAN per Browser eine neue `dashboard.json` aufs Gerät laden.
Das Gerät validiert die Datei, ersetzt die alte nur bei Erfolg und startet neu.

## Nicht-Ziele (bewusst später / YAGNI)

Die Architektur hält die Andockpunkte für diese Erweiterungen offen, v1
implementiert sie aber **nicht**:

- **AP-Modus** (Gerät spannt eigenes WLAN auf) — als Funktion in `wifi_port`
  vorgesehen, in v1 ungenutzt.
- **Web-Konfigurator** — Browser-UI, die Widgets/Signale auswählt und die
  `dashboard.json` clientseitig baut. Wird durch die Asset-von-SD-Strategie
  (s. u.) ohne Firmware-Update möglich.
- **Touchscreen-Systemseite** zum Konfigurieren/Statusanzeigen.
- **WLAN-Status-Icon** in einer Dashboard-Ecke (mit Sprung auf Detailseite).
  `wifi_port` bietet dafür eine abfragbare Statusfunktion.
- **Adress-Anzeige am Display** (Hostname/IP als Banner oder Statuszeile) — kommt
  zusammen mit der Detailseite/dem Status-Icon. In v1 ist der Primärzugang
  `dashboard.local` (mDNS); als Rückfall wird die IP beim Verbinden in den
  **seriellen Log** geschrieben.
- **Authentifizierung** der Upload-Seite — v1 vertraut dem lokalen WLAN als
  Grenze; Passwortschutz ist additiv nachrüstbar.

## Schlüsselentscheidungen

### 1. Web-Assets von der SD ausliefern (mit Flash-Fallback)

Der HTTP-Server liefert die Web-Oberfläche (`/www/index.html`, `app.js`,
`style.css`) **von der SD-Karte** aus. Der Upload-Endpunkt (`POST /api/config`)
ist fest in der Firmware. Fehlt `/www` auf der SD, liefert der Server ein
minimales, in den Flash eingebettetes Fallback-HTML mit reinem Upload-Formular.

**Begründung:** Der spätere Web-Konfigurator wird damit zu reinem JavaScript,
das clientseitig die `dashboard.json` baut und über denselben `POST`-Endpunkt
hochlädt — **ohne Firmware-Update**. Die Firmware stellt nur eine stabile
Mini-API + statisches File-Serving bereit; die UI entwickelt sich frei weiter.
Das Flash-Fallback nimmt der SD-Abhängigkeit das einzige echte Risiko.

### 2. WLAN-Modus STA mit Credential-Liste auf SD

Das Gerät bucht sich in ein vorhandenes WLAN ein (STA). Die Zugangsdaten liegen
als `wifi.json` auf der SD: ein Array von `{"ssid":..., "password":...}`, das
der Reihe nach durchprobiert wird. Format konsistent zur `dashboard.json`
(cJSON ist bereits im Projekt). Keine Credentials in Kconfig/Flash.

### 5. mDNS als Primärzugang

Nach erfolgreicher Verbindung registriert das Gerät einen mDNS-Hostnamen
(`dashboard.local`), sodass der Browser ohne Kenntnis der IP zugreifen kann.
Die IP wird zusätzlich in den seriellen Log geschrieben (Rückfall, falls mDNS
am Client nicht verfügbar ist). Eine Adress-Anzeige am Display ist v1 nicht
Teil (s. Nicht-Ziele).

### 3. Validieren vor Übernehmen

Ein fehlerhafter Upload darf das Gerät nie unbrauchbar machen. Der Upload landet
zuerst in `dashboard.json.tmp`, wird mit dem bestehenden `config_loader`
geparst, und erst bei Erfolg atomar per `rename` zur aktiven `dashboard.json`.

### 4. Reboot statt Live-Reload

Nach erfolgreichem Upload startet das Gerät neu und lädt die JSON über den
normalen, bereits validierenden Boot-Pfad. Kein riskanter Laufzeit-Abbau der
LVGL-Widgets.

## Architektur

Einordnung gemäß Constitution (Prinzip I: HAL/App-Trennung; II: C-only;
III: nativ testbare App-Logik; IV: Kconfig; V: LVGL-Thread-Safety).

### Neue Module

| Modul | Layer | Aufgabe | ESP-IDF-abhängig |
|-------|-------|---------|------------------|
| `hal/waveshare_wifi_port.c/h` | HAL | WiFi-STA verbinden (`esp_wifi`/`esp_netif`/`nvs`), AP-Liste der Reihe nach mit Timeout durchprobieren, periodischer Retry. Abfragbarer Status (`wifi_port_get_status()`). AP-Modus-Funktion vorgesehen, v1 ungenutzt. | ja (gekapselt) |
| `hal/web_server.c/h` | HAL | `esp_http_server` kapseln: statische Assets von SD ausliefern (`/www/*`), Flash-Fallback-HTML, `POST /api/config`-Handler | ja (gekapselt) |
| `app/wifi_credentials.c/h` | App | `wifi.json` von SD parsen → Liste `{ssid, password}`. Reines cJSON-Parsing, **nativ testbar** | nein |

### Wiederverwendet

- `app/config_loader` — validiert den Upload (identischer Pfad wie Boot, kein Duplikat).
- `hal/waveshare_sd_port` — Dateien lesen/schreiben (ggf. um Schreib-/Rename-Funktion ergänzen).

### Kconfig (Prinzip IV)

- `CONFIG_DASHBOARD_WIFI_ENABLE` — Feature an/aus (Gerät ohne WLAN voll funktionsfähig; Feature ist additiv).
- HTTP-Port (Default 80).
- `/www`-Basispfad auf SD.
- WLAN-Verbindungs-Timeout und Retry-Intervall.

### Boot-Reihenfolge (Performance: Dashboard < 3 s)

1. Display + LVGL init, SD mount, `dashboard.json` laden, Dashboard rendern — **wie heute**.
2. **Danach**: WiFi-Verbindung + Webserver in einem **eigenen Hintergrund-Task** starten.
   Blockiert den Display-Start nicht. Schlägt WLAN fehl, läuft das Dashboard normal weiter.

## Datenfluss

### Upload (kritischer Pfad)

```
Browser: POST /api/config (multipart, dashboard.json)
   │
   ▼
web_server schreibt Body → /sdcard/dashboard.json.tmp   (Original unberührt)
   │
   ▼
config_loader_parse(tmp) ── FEHLER ─► tmp löschen, HTTP 400 + Fehlermeldung
   │                                   (altes dashboard.json bleibt aktiv)
   ▼ OK
rename tmp → dashboard.json   (atomar)
   │
   ▼
HTTP 200 "OK, Neustart…"  ─►  Antwort senden  ─►  esp_restart()
```

- Original wird erst angefasst, wenn die neue Datei validiert ist.
- Browser-Fehlermeldung = dieselbe, die `config_loader` heute erzeugt.
- `esp_restart()` erst **nach** Absenden der HTTP-200-Antwort.

### WiFi-Verbindung (Hintergrund-Task)

- `wifi.json` lesen → AP-Liste. Fehlt/leer → Webserver bleibt aus, Dashboard läuft, Log-Hinweis.
- APs der Reihe nach mit Timeout. Erster Erfolg gewinnt → IP in seriellen Log schreiben, mDNS-Hostname `dashboard.local` registrieren.
- Kein AP erreichbar → Log-Warnung + periodischer Retry. Dashboard unbeeinträchtigt.

## Fehlerbehandlung

| Situation | Reaktion |
|-----------|----------|
| `wifi.json` fehlt/leer | Webserver aus, Dashboard läuft, Log-Hinweis |
| Kein AP erreichbar | Periodischer Retry, Dashboard läuft |
| Upload ungültige JSON | HTTP 400 + `config_loader`-Meldung, altes Dashboard bleibt |
| Upload zu groß (> Limit) | HTTP 413, Abbruch (Limit = `s_json_buf`-Größe, 16 KB) |
| SD-Schreibfehler | HTTP 500 + Meldung, kein Reboot |
| `/www` fehlt auf SD | Eingebettetes Fallback-HTML aus Flash |

## Testbarkeit (Constitution III)

| Was | Testart | Inhalt |
|-----|---------|--------|
| `app/wifi_credentials` | Nativ (Unity, `pio test -e native`) | valide AP-Liste, mehrere APs, leere Liste, fehlende Felder, kaputte JSON |
| Upload-Validierung | Nativ (bereits abgedeckt) | nutzt getesteten `config_loader` |
| `temp → rename`-Logik | HW | FATFS-abhängig, Quickstart-Schritt |
| `wifi_port`, `web_server` | HW | ESP-IDF/Hardware, Quickstart-Validierung wie T038 |

Test-First: Die neue portable Logik (`wifi_credentials`) bekommt rote
Unity-Tests vor der Implementierung. ESP-IDF-Glue wird per HW-Quickstart geprüft.

## Offene Punkte für die Implementierungsplanung

- Multipart-Parsing in `esp_http_server`: einfacher Raw-Body-POST vs.
  `multipart/form-data`. Tendenz: simpler `POST` mit Rohinhalt für v1, das
  HTML-Formular sendet entsprechend.
- Genaues `wifi.json`-Schema (Feldnamen, optionale Felder) im Plan festzurren.

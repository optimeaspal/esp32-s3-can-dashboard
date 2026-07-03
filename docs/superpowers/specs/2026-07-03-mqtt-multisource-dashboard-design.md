# Feature 006 – Protokoll-agnostisches Dashboard (MQTT, USB-/WLAN-Netz)

**Datum:** 2026-07-03 · **Überarbeitet:** 2026-07-04
**Status:** Programm-Design abgenommen, bereit für Phasen-Pläne
**Art:** Mehrphasiges Programm – dieses Dokument ist der Programm-Rahmen.
Jede Phase erhält beim Start ihre **eigene** Spec (`…-design.md`) + Plan.

> **Revision 2026-07-04:** Ein Hardware-Constraint (USB und CAN teilen sich
> GPIO19/20) macht gleichzeitigen Betrieb unmöglich. Das Design löst das über
> **umschaltbare Betriebsmodi** mit USB-first-Boot-Logik und WLAN/SoftAP-
> Fallback (siehe „Hardware-Constraint" und „Betriebsmodi"). Der USB-Netz-Teil
> bleibt der primäre Commissioning-Weg.

## Ziel & Motivation

Das Dashboard soll vom reinen **CAN**-Anzeigegerät zu einem
**protokoll-agnostischen, bidirektionalen** Dashboard werden. Die CAN-Schiene
bleibt bestehen; **MQTT** kommt als gleichberechtigte Datenquelle *und* als
Rückkanal hinzu.

Leitbild ist ein **vollständig autarkes Gerät ohne externe Infrastruktur**:
Man steckt es per **USB** an einen PC – das Gerät stellt darüber selbst ein
Netzwerk bereit und hostet alle Dienste on-device (DHCP, mDNS, den bestehenden
HTTP-Editor, einen **MQTT-Broker**). Kein Router, kein fremder Broker nötig.
Ist kein USB-Host angeschlossen, arbeitet das Gerät eigenständig als
**CAN-Display** und stellt sein Netz per WLAN bereit (eigener AP oder
Verbindung zu einem vorhandenen AP).

## Hardware-Constraint (die zentrale Randbedingung)

**USB und CAN können auf diesem Board nicht gleichzeitig laufen** – sie teilen
sich physisch dieselben Pins:

- **Silizium:** Beim ESP32-S3 liegen die nativen **USB-D-/D+-Leitungen fest auf
  GPIO19/GPIO20**.
- **Board:** CAN nutzt genau diese Pins (`TWAI_TX=GPIO20`, `TWAI_RX=GPIO19`).
- **Mux:** Der CH422G-IO-Expander hat eine **`USB_SEL`**-Leitung, die GPIO19/20
  per Hardware umschaltet – Beleg im Code
  (`src/hal/waveshare_rgb_lcd_port.c`, `waveshare_rgb_lcd_can_mux_enable()`):
  `// 0x1E = Backlight-Bits, 0x20 = USB_SEL HIGH → CAN; kombiniert 0x3E`.
  `USB_SEL HIGH → CAN` (Pins am Transceiver), `LOW → USB` (Pins am Stecker).

**Wichtig:** WLAN nutzt den **Funk-Teil**, nicht GPIO19/20. Deshalb ist CAN mit
**jedem WLAN-Modus (STA *oder* SoftAP) gleichzeitig** möglich – **nur USB**
schließt CAN aus.

## Betriebsmodi

Drei Personas, über die Mux-Stellung + WLAN-Modus:

| Erkannt beim Boot | Modus | Mux (`USB_SEL`) | CAN | Netz für Dienste/MQTT |
|---|---|---|---|---|
| USB-Host angeschlossen | **USB-NET** | USB (LOW) | ❌ | USB-LAN (Gadget) |
| kein USB, AP konfiguriert | **WLAN-STA** | CAN (HIGH) | ✅ | externes LAN |
| kein USB, kein AP | **WLAN-SoftAP** | CAN (HIGH) | ✅ | eigener AP |

**Boot-Logik – „steckt ein USB-Host dran?" entscheidet (USB-first):**

1. Boot → Mux→USB, USB-Netz-Gadget hoch, **auf Host-Enumeration warten**
   (`USB_PROBE_TIMEOUT`, Default z. B. 3 s).
2. **Host erkannt → USB-NET** (kein CAN). → Standardfall Inbetriebnahme am PC.
3. **Kein Host im Zeitfenster → Mux→CAN, WLAN hoch:** WLAN-Creds vorhanden →
   **STA** (Büro/Labor); sonst/scheitert → **SoftAP** (Feld/standalone).
   **CAN läuft in beiden WLAN-Fällen.**
4. **Manuelle Übersteuerung** möglich (Settings-Screen: „In USB-/WLAN-Modus
   wechseln → Reboot") für Sonderfälle; persistierter Wunschmodus in NVS.

**Designprinzipien:**
- **CAN hängt nie von einem externen AP ab** – der SoftAP-Fallback garantiert
  ein eigenständiges CAN-Display mit Netzzugang.
- **Moduswechsel = Reboot.** `USB_SEL` umzuschalten routet GPIO19/20 hardware-
  seitig um und erfordert Neu-Init von TWAI bzw. USB-PHY – kein Runtime-Hotswap.
- **Anti-Lockout:** USB-Host da → immer USB-NET erreichbar; kein USB → immer
  mindestens SoftAP erreichbar. Man sperrt sich nie aus.

## Kernidee – Das einheitliche Signal-Modell

**Für die UI existieren nur noch Signalnamen.** Ein Signal ist eine benannte
Größe mit bis zu zwei Richtungen; *wohin* ein Zugriff geht, steht in der
**Signaldefinition**, nicht im Widget.

**Lesepfad (Anzeige)** – Wert aus genau einer Quelle:

| `signal.source` | Bedeutung |
|---|---|
| `can` | CAN-Frame dekodieren (wie heute) |
| `mqtt` | Topic abonnieren, Payload parsen |
| `sim` | Simulator |

**Schreibpfad (Bedienung)** – ein Signal *setzen* löst genau eine Aktion aus:

| `signal.write` | Bedeutung |
|---|---|
| `can_tx` | Wert → CAN-Frame kodieren + über TWAI senden (generalisiert das bisher reservierte `tx_commands`) |
| `mqtt_pub` | Wert → auf ein Topic publishen (`retain` optional) |

**Kein protokollübergreifendes Routing:** Ein MQTT-Empfang löst nie ein
CAN-Senden aus (und umgekehrt). Jede Signal-Schreibrichtung hat **genau eine
Sink**. Es gibt nur `MQTT→Input` / `CAN→Input` (Anzeige) und
`MQTT→Publish` / `CAN→Send` (Bedienung).

**Verhalten über Modi:** Ein Widget, dessen Signal an einen im aktuellen Modus
nicht verfügbaren Bus gebunden ist (z. B. CAN-Signal im USB-NET-Modus), bleibt
schlicht **stale** (ausgegraut) – die vorhandene Stale-Logik deckt das ab.

**Wert-Typ:** vorerst **rein numerisch** (`float`). Alle Eingabe-Widgets in
diesem Programm sind numerisch.

**Widget-Typen:**
- **Anzeige (Lesen), bestehend:** `gauge`, `arc`, `chart`, `bar`, `led`, `label`
- **Eingabe (Schreiben), neu:** `push-button` (momentan/Trigger),
  `radio-switch` (1-aus-N), `check-button` (Boolean-Toggle),
  `numeric-edit` (Zahl)

## Zielarchitektur

```
                 ┌──────────── Betriebsmodus (Boot-Probe, NVS) ────────────┐
                 │  USB-Host? → USB-NET   |   sonst → WLAN-STA / SoftAP     │
                 └───────────────┬─────────────────────┬──────────────────┘
                          Mux→USB │                     │ Mux→CAN
                 USB-Netz-Gadget ─┘                     └─ WLAN (STA/AP) + CAN aktiv
                          └───────────────┬─────────────────────┘
                       on-device: DHCP · mDNS · HTTP-Editor · MQTT-Broker
                                          │
                                data_source-Abstraktion
                          ┌───────────────┼───────────────┐
                        source_can     source_mqtt      (sim)
                          └───────────────┼───────────────┘
                                 Signal-/Widget-Layer (nur Signalnamen)
                                          │  signal_set(idx, value)
                                ┌─────────┴─────────┐
                              can_tx (TWAI)     sink_mqtt (publish)
```

Alles fügt sich in die bestehende `hal/`·`app/`·`ui/`-Trennung
(Constitution Prinzip I) ein. Protokoll-/Parsing-Logik bleibt **nativ testbar**
im `app/`-Layer ohne ESP-IDF-Abhängigkeit.

## Ansatz: Hardware/Transport zuerst (de-risk)

Gewählter Ansatz **A**: Das größte Unbekannte – **funktioniert USB-OTG als
Netzwerk-Gadget auf diesem Board?** – wird als **erster Task in Phase 1**
verifiziert. Der Broker wird in **reinem C** selbst gebaut (bekannte kleine
Broker wie PicoMQTT/sMQTTBroker sind C++/Arduino und kollidieren mit
„C-first"); die Protokoll-Logik ist dadurch test-first abdeckbar.

## Phasenschnitt

Jede Phase ist unabhängig lieferbar und testbar und erhält eigene Spec+Plan.

### Phase 1 – Betriebsmodus & Netz-Transport (kein MQTT)

- **USB-OTG-Spike zuerst:** TinyUSB-Netzwerkklasse (**NCM** primär, **RNDIS**
  Fallback für ältere Windows) bringen und am USB-C-Stecker enumerieren →
  klärt die Machbarkeit, bevor darauf gebaut wird.
- `hal/usb_net_port.{c,h}` **(neu)** – USB-Netz-Gadget + interner DHCP-Server +
  geräteeigene IP + `esp_netif`.
- `hal/waveshare_wifi_port` **(erweitern)** – **SoftAP-Modus** ergänzen
  (Header nennt AP-Modus bereits als vorgesehene Erweiterung); STA bleibt.
- `app/net_mode.{c,h}` **(neu, nativ testbar)** – Mode-Manager: Boot-Probe-
  Logik, NVS-Persistenz (Wunschmodus + WLAN-Creds), STA→SoftAP-Fallback,
  Mux-Ansteuerung modusabhängig (Backlight-Bits `0x1E` erhalten, nur `USB_SEL`
  variieren).
- **Web-Frontend:** WLAN-Konfig-Formular (SSID/Passwort speichern → Reboot in
  WLAN-Modus). Baut auf dem bestehenden `web_server` + Editor auf.
- **Liefergegenstand:** USB anstecken → PC bekommt Netz → `dashboard.local`-
  Editor über USB. Kein USB → CAN läuft, Gerät als STA **oder** SoftAP
  erreichbar; WLAN im Browser konfigurierbar. **Noch kein MQTT.**

### Phase 2 – Source-Abstraktion + On-device-Broker + MQTT-Lesen

- `app/data_source.{c,h}` **(neu)** – Quellen-Schnittstelle + Registry.
- `app/source_can.{c,h}` **(neu)** – kapselt den bestehenden `can_dispatcher`
  als Source; Verhalten unverändert, native Tests bleiben grün.
- `app/mqtt_broker_core.{c,h}` **(neu, nativ testbar, TEST-FIRST)** –
  MQTT-3.1.1-State-Machine in reinem C: CONNECT/CONNACK, SUB/SUBACK,
  PUBLISH-Fan-out, PING, QoS 0/1.
- `hal/mqtt_broker_net.{c,h}` **(neu)** – lwIP-Socket-Loop, ruft den Core auf.
- `app/source_mqtt.{c,h}` **(neu)** – abonniert intern Topics → speist Signale.
- Config: `signal.source = can|mqtt|sim` + quellenspezifische Felder.
- **Liefergegenstand:** PC-MQTT-Client verbindet zum Gerät; gepublishte Werte
  erscheinen auf dem Display.

### Phase 3 – Rückkanal (Schreibpfad + Eingabe-Widgets)

- `app/can_tx.{c,h}` **(neu)** – Wert → CAN-Frame kodieren + über TWAI senden.
- `app/sink_mqtt.{c,h}` **(neu)** – Wert → Topic publishen (`retain` optional).
- **`signal_set(idx, value)`-Routing** – je `signal.write` an die richtige Sink.
- `ui/widgets/` **(neu)**: `push-button`, `radio-switch`, `check-button`,
  `numeric-edit`; `dashboard` verarbeitet deren Touch-Events → `signal_set`.
- Config: `signal.write`-Binding + neue Widget-Typen.
- **Liefergegenstand:** Bedienelement am Display → CAN-Frame **oder**
  MQTT-Publish; der PC steuert per MQTT zurück.

## Scope

**Enthalten:** Betriebsmodus-Umschaltung (USB-NET / WLAN-STA / SoftAP) mit
USB-first-Boot + Fallback; on-device-MQTT-Broker (reines C, test-first); MQTT
als Lesequelle; bidirektionaler Rückkanal (CAN-TX **oder** MQTT-Publish) über
neue Eingabe-Widgets, geroutet über die Signaldefinition.

**Bewusst NICHT (YAGNI / Backlog):**
- **OPC-UA** – später oder nie; kein Bestandteil dieses Programms.
- **`text-edit` / String-Werte** – Wertemodell bleibt numerisch.
- **CAN↔MQTT-Routing/Mirror** – keine protokollübergreifende Übersetzung.
- **Gleichzeitig CAN + USB** – hardwareseitig auf diesem Board unmöglich.

## Zukunft / Backlog

- **Eigenes ESP32-S3-Board** mit anderer GPIO-Belegung **oder** einem
  **SPI-angebundenen CAN-Controller (z. B. MCP2515)** → dann CAN **und** USB
  gleichzeitig, ohne Mux und ohne Modus-XOR. Dies würde den
  Betriebsmodus-Zwang aus diesem Design langfristig auflösen.

## Constitution-Bezug

- **I. HAL/App-Trennung:** Netz-/Socket-/Mux-Code in `hal/`
  (`usb_net_port`, `mqtt_broker_net`, `waveshare_wifi_port`,
  `waveshare_rgb_lcd_port`); Modus-Entscheidung/Persistenz-Logik und Protokoll-/
  Parsing-Logik in `app/` (`net_mode`, `mqtt_broker_core`, `data_source`,
  `config_loader`-Erweiterung) ohne ESP-IDF-Include → nativ testbar.
- **II. C-First:** alles in `.c`/`.h`; Broker bewusst selbst in C.
- **III. Test-First:** `net_mode` (Boot-/Fallback-Entscheidungslogik),
  `mqtt_broker_core` und die `config_loader`-Erweiterungen bekommen rote
  Unity-Tests vor der Implementierung.
- **IV. Kconfig:** `USB_PROBE_TIMEOUT`, USB-Netz-Parameter (IP-Bereich, DHCP),
  SoftAP-SSID/-Kanal, MQTT-Broker-Port und Limits (max. Clients/Subscriptions)
  als Kconfig-Symbole/`#define`.
- **V. LVGL-Thread-Safety:** neue Eingabe-Widgets und `signal_set` aus der UI
  unter `lvgl_port_lock`; Broker-/Source-Tasks reichen Werte über die
  bestehende Queue-Mechanik, nicht direkt in LVGL.

## Hinweise / Watchlist

- **USB-OTG-Machbarkeit** (Pins am Stecker vs. nur Serial-JTAG) – auf dem
  Hauptpfad, deshalb erster Task in Phase 1.
- **Host-Erkennung:** Enumeration/SOF im Probe-Fenster muss verlässlich sein;
  Fallback = persistierter Modus + Settings-Toggle statt Auto-Probe.
  Im No-USB-Fall entsteht ein kurzes CAN-Startverzögerungsfenster
  (= Probe-Timeout) – konfigurierbar, unkritisch beim Power-on.
- **Serieller Monitor:** Im CAN-Modus liegen GPIO19/20 am Transceiver – am
  USB-Stecker ist dann vermutlich **kein USB-Serial-Console** verfügbar.
  Prüfen, ob das Board einen separaten UART-Bridge-Chip hat; sonst Diagnose
  über den Settings-/Info-Screen + Netz-Logging.
- **`esp_netif` / Event-Loop / NVS** werden heute in `waveshare_wifi_port`
  einmalig initialisiert – bei mehreren Netz-Wegen die Einmal-Initialisierung
  sauber zentralisieren (Doppel-Init vermeiden).
- **Interner RAM/DMA ist knapp** (RGB-Framebuffer + LVGL): Broker-Puffer und
  Client-/Subscription-Tabellen klein und fest dimensionieren; große Puffer
  ggf. ins PSRAM. Broker zunächst auf **wenige Clients** auslegen.
- **`lv_label_set_text_fmt` kann kein `%f`** (bekannt aus Feature 005):
  Float-Ausgaben über `snprintf` + `lv_label_set_text`.

## Offene Punkte für die Phasen-Pläne

- **Phase 1:** USB-Netzklasse endgültig (NCM/RNDIS/Composite); DHCP-Bereich +
  Gerät-IP (USB & SoftAP); `USB_PROBE_TIMEOUT`-Default; NVS-Layout für
  Wunschmodus + WLAN-Creds; SoftAP-SSID/-Passwort-Policy.
- **Phase 2:** Topic-Namensschema (`signal.name` ↔ Topic); Payload-Format beim
  Lesen (roher Zahlwert vs. JSON); QoS-Umfang; Broker-Limits
  (Clients/Subscriptions/Payload-Größe).
- **Phase 3:** JSON-Schema für `can_tx` (Byte-Layout, Endianness, Scale/Offset
  rückwärts) und `mqtt_pub` (Topic, Payload-Format, `retain`); Semantik von
  `push-button` (gesendeter Wert) und `radio-switch` (Wertetabelle je Option).

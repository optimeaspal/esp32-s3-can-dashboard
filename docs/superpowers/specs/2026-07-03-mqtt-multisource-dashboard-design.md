# Feature 006 – Protokoll-agnostisches Dashboard (MQTT über USB-Netz)

**Datum:** 2026-07-03
**Status:** Programm-Design abgenommen, bereit für Phasen-Pläne
**Art:** Mehrphasiges Programm – dieses Dokument ist der Programm-Rahmen.
Jede Phase erhält beim Start ihre **eigene** Spec (`…-design.md`) + Plan.

## Ziel & Motivation

Das Dashboard soll von einem reinen **CAN**-Anzeigegerät zu einem
**protokoll-agnostischen, bidirektionalen** Dashboard werden. Die CAN-Schiene
bleibt bestehen; MQTT kommt als **gleichberechtigte Datenquelle** und als
**Rückkanal** hinzu.

Leitbild ist ein **vollständig autarkes Gerät**: Man steckt es per **USB** an
einen PC, das Gerät stellt darüber selbst ein Netzwerk bereit und hostet alle
Dienste on-device (DHCP, mDNS, den bestehenden HTTP-Editor und einen
**MQTT-Broker**). Keine externe Infrastruktur – kein Router, kein fremder
Broker, kein WLAN nötig.

## Kernidee – Das einheitliche Signal-Modell

**Für die UI existieren nur noch Signalnamen.** Ein Signal ist eine benannte
Größe mit bis zu zwei Richtungen; *wohin* ein Zugriff geht, steht in der
**Signaldefinition**, nicht im Widget. Ein Widget sagt nur „zeige Signal X"
bzw. „setze Signal X".

**Lesepfad (Anzeige)** – ein Signal bezieht seinen Wert aus genau einer Quelle:

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

Ein Signal kann **rein lesend** (nur Quelle), **rein schreibend** (nur Sink)
oder **beides** sein.

**Wert-Typ:** vorerst **rein numerisch** (`float`, wie heute). Alle Eingabe-
Widgets in diesem Programm sind numerisch. String-Werte kämen erst mit
`text-edit` (→ Backlog).

**Widget-Typen:**

- **Anzeige (Lesen), bestehend:** `gauge`, `arc`, `chart`, `bar`, `led`, `label`
- **Eingabe (Schreiben), neu:** `push-button` (momentan/Trigger),
  `radio-switch` (1-aus-N), `check-button` (Boolean-Toggle),
  `numeric-edit` (Zahl)

## Zielarchitektur

```
PC  ⇄  USB-Netz-Gadget (NCM/RNDIS)
        └─ on-device: DHCP · mDNS · HTTP-Editor · MQTT-Broker
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

Gewählter Ansatz **A**: Das größte Unbekannte – **läuft USB-OTG als
Netzwerk-Gadget auf diesem Board?** – wird in Phase 1 verifiziert, **bevor**
MQTT darauf gestapelt wird. Der Broker wird in **reinem C** selbst gebaut
(bekannte kleine Broker wie PicoMQTT/sMQTTBroker sind C++/Arduino und
kollidieren mit „C-first"); die Protokoll-Logik ist dadurch test-first
abdeckbar.

### ⚠ Hardware-Risiko (Phase-0-Recherche für Phase 1)

Das ESP32-S3 hat **zwei** USB-Peripherien: **USB-Serial-JTAG** (Flashen +
serieller Monitor) und **USB-OTG** (TinyUSB, kann die Netzwerkklasse). Nur der
OTG-Controller kann das Netz-Gadget. **Zu verifizieren:** Ob der USB-C-Stecker
des Waveshare-ESP32-S3-Touch-LCD-7 an OTG oder nur an Serial-JTAG hängt. Hängt
er nur an Serial-JTAG, braucht es den zweiten USB-Port/die D+/D−-Pins – oder der
Transport muss umdisponiert werden. **Diese Recherche entscheidet die
Machbarkeit von Phase 1 und ist deren erster Schritt.**

## Phasenschnitt

Jede Phase ist unabhängig lieferbar und testbar und erhält eigene Spec+Plan.

### Phase 1 – Autarker Netzknoten über USB (kein MQTT)

- `hal/usb_net_port.{c,h}` **(neu)** – TinyUSB-Netzwerkklasse (**NCM** primär,
  **RNDIS**-Fallback für ältere Windows), interner **DHCP-Server**,
  geräteeigene IP, `esp_netif`-Anbindung.
- Bestehende Netz-Dienste an das USB-`netif` binden: der `web_server`
  (HTTP-Editor) und **mDNS** `dashboard.local` laufen über die USB-Strecke.
- **Liefergegenstand:** USB anstecken → PC bekommt per DHCP eine IP →
  `http://dashboard.local`-Editor funktioniert über USB, ohne WLAN.
- **Verifiziert** die USB-OTG-Machbarkeit als Fundament.

### Phase 2 – Source-Abstraktion + On-device-Broker + MQTT-Lesen

- `app/data_source.{c,h}` **(neu)** – Quellen-Schnittstelle + Registry
  (`open`, `poll`/Callback, `bind_signal`).
- `app/source_can.{c,h}` **(neu)** – kapselt den bestehenden `can_dispatcher`
  als *eine* Source hinter der Abstraktion; Verhalten unverändert, native Tests
  bleiben grün.
- `app/mqtt_broker_core.{c,h}` **(neu, nativ testbar, TEST-FIRST)** –
  MQTT-3.1.1-Protokoll-State-Machine in reinem C: CONNECT/CONNACK,
  SUBSCRIBE/SUBACK, PUBLISH-Fan-out, PINGREQ/RESP, QoS 0/1.
- `hal/mqtt_broker_net.{c,h}` **(neu)** – lwIP-Socket-Loop (accept/read/write),
  ruft den Core auf (HAL/App-Trennung gewahrt).
- `app/source_mqtt.{c,h}` **(neu)** – abonniert intern Topics → speist Signale.
- Config: `signal.source = can|mqtt|sim` + quellenspezifische Felder.
- **Liefergegenstand:** PC-MQTT-Client verbindet zum Gerät; auf ein Topic
  gepublishte Werte erscheinen auf dem Display.

### Phase 3 – Rückkanal (Schreibpfad + Eingabe-Widgets)

- `app/can_tx.{c,h}` **(neu)** – Wert → CAN-Frame kodieren + über TWAI senden
  (generalisiert `tx_commands`).
- `app/sink_mqtt.{c,h}` **(neu)** – Wert → Topic publishen (`retain` optional).
- **`signal_set(idx, value)`-Routing** – setzt den Wert und leitet je nach
  `signal.write = can_tx|mqtt_pub` an die richtige Sink.
- `ui/widgets/` **(neu)**: `push-button`, `radio-switch`, `check-button`,
  `numeric-edit`; `dashboard` verarbeitet deren Touch-Events → `signal_set`.
- Config: `signal.write`-Binding + neue Widget-Typen.
- **Liefergegenstand:** Bedienelement am Display → sendet CAN-Frame **oder**
  MQTT-Publish; der PC steuert per MQTT zurück.

## Scope

**In diesem Programm enthalten:**
- USB-Netz-Gadget als autarker Transport inkl. DHCP/mDNS
- On-device-MQTT-Broker (reines C, test-first)
- MQTT als Lesequelle für Widgets
- Bidirektionaler Rückkanal (CAN-TX **oder** MQTT-Publish) über neue
  Eingabe-Widgets, geroutet über die Signaldefinition

**Bewusst NICHT enthalten (YAGNI / Backlog):**
- **OPC-UA** – später oder nie; kein Bestandteil dieses Programms
- **`text-edit` / String-Werte** – Wertemodell bleibt numerisch
- **CAN→MQTT-Read-Mirror** – ein Signal ist entweder CAN- *oder* MQTT-gelesen;
  Publishen entsteht ausschließlich über den Schreibpfad
- **WLAN als Alternativtransport** – WLAN existiert bereits (Upload-Feature);
  USB ist der neue Fokus, keine Parallel-Pflege in diesem Programm

## Constitution-Bezug

- **I. HAL/App-Trennung:** Netz-/Socket-Code in `hal/`
  (`usb_net_port`, `mqtt_broker_net`); Protokoll-/Parsing-Logik in `app/`
  (`mqtt_broker_core`, `data_source`, `config_loader`-Erweiterung) ohne
  ESP-IDF-Include → nativ testbar.
- **II. C-First:** alles in `.c`/`.h`; Broker bewusst selbst in C (kein C++).
- **III. Test-First:** `mqtt_broker_core` und die `config_loader`-Erweiterungen
  (neue `source`/`write`-Bindings) bekommen rote Unity-Tests vor der
  Implementierung.
- **IV. Kconfig:** USB-Netz-Parameter (IP-Bereich, DHCP), MQTT-Broker-Port und
  Limits (max. Clients, max. Subscriptions) als Kconfig-Symbole/`#define`.
- **V. LVGL-Thread-Safety:** neue Eingabe-Widgets und `signal_set` aus der UI
  laufen unter `lvgl_port_lock`; die Broker-/Source-Tasks reichen Werte über
  die bestehende Queue-Mechanik, nicht direkt in LVGL.

## Hinweise / Watchlist

- **Interner RAM/DMA ist knapp** (RGB-Framebuffer + LVGL): Broker-Puffer und
  Client-/Subscription-Tabellen **klein und fest** dimensionieren; große Puffer
  ggf. ins PSRAM. Broker zunächst auf **wenige Clients** (PC) auslegen.
- **USB-OTG vs. USB-Serial-JTAG** (siehe Hardware-Risiko) – blockierend für
  Phase 1, zuerst klären.
- **Windows-Kompatibilität der USB-Netzklasse:** NCM (Win 11 nativ) vs. RNDIS
  (ältere Windows) – Klasse/Fallback in Phase 1 festlegen.
- **`lv_label_set_text_fmt` kann kein `%f`** (bekannt aus Feature 005):
  Float-Ausgaben über `snprintf` + `lv_label_set_text`.

## Offene Punkte für die Phasen-Pläne

- **Phase 1:** USB-Netzklasse endgültig festlegen (NCM/RNDIS/Composite);
  DHCP-Adressbereich + Gerät-IP; Verhältnis zu WLAN (koexistieren oder USB
  vorrangig?).
- **Phase 2:** Topic-Namensschema (`signal.name` ↔ Topic-Mapping);
  Payload-Format beim Lesen (roher Zahlwert vs. JSON); QoS-Umfang;
  Broker-Limits (max. Clients/Subscriptions/Payload-Größe).
- **Phase 3:** JSON-Schema für `can_tx`-Kodierung (Byte-Layout, Endianness,
  Scale/Offset rückwärts) und `mqtt_pub` (Topic, Payload-Format, `retain`);
  Semantik von `push-button` (welcher Wert wird gesendet) und `radio-switch`
  (Wertetabelle je Option).
```

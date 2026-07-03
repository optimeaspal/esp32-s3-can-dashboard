# Briefing: Erweiterung des CAN-Dashboards um MQTT & Netzwerk-Betrieb

**Von:** Burkhard Schranz
**An:** den Autor des aktuellen Firmware-Standes
**Datum:** 2026-07-04
**Zweck:** Dich über die geplante Erweiterung informieren – Idee, Hintergründe,
die zentrale Hardware-Erkenntnis und die nächsten Schritte. Kein fertiger
Auftrag, sondern eine Einladung, gemeinsam draufzuschauen und dein Feedback
einzubringen. Die formale Programm-Spec liegt unter
[`docs/superpowers/specs/2026-07-03-mqtt-multisource-dashboard-design.md`](superpowers/specs/2026-07-03-mqtt-multisource-dashboard-design.md).

---

## 1. Worum es geht

Dein CAN-Dashboard soll vom reinen **CAN-Anzeigegerät** zu einem
**protokoll-agnostischen, bidirektionalen** Dashboard werden:

- **CAN bleibt** vollständig erhalten.
- **MQTT** kommt hinzu – als gleichberechtigte **Datenquelle** (Werte vom
  Netzwerk aufs Display) **und** als **Rückkanal** (Bedienelemente am Display,
  die etwas senden/publishen).
- Das Gerät soll **autark** sein: kein externer Broker, kein zwingender Router.
  Es hostet Netz + Broker selbst.
- **OPC-UA** ist als ferne Option notiert, aber **nicht** Teil dieses Programms
  („später oder nie").

Der Grundgedanke bei der Bedienung: **Die UI kennt künftig nur noch
Signalnamen.** Ob ein Signal von CAN oder MQTT kommt bzw. wohin ein „Setzen"
geht (CAN-Frame senden oder MQTT publishen), steht in der **Signaldefinition** –
nicht im Widget. Damit wird aus dem CAN-Dashboard ein universelles Dashboard,
und CAN ist einfach eine Quelle von mehreren.

## 2. Warum das gut zu deinem Code passt

Vorab: Die vorhandene Architektur trägt diese Erweiterung erstaunlich gut – das
ist ausdrücklich als Kompliment gemeint. Konkret bauen wir auf deinen
Entscheidungen auf:

- Deine **saubere `hal/` · `app/` · `ui/`-Trennung** ist genau die Naht, an der
  wir neue Quellen/Sinks andocken, ohne die UI anzufassen.
- Der **`widget_registry`**-Ansatz (Funktionstabelle `type → create/update`)
  macht die neuen Eingabe-Widgets zu reinen zusätzlichen Registry-Einträgen.
- **`config_loader`** als nativ testbares Modul ist die Blaupause dafür, wie wir
  die neuen `source`/`write`-Bindings test-first ergänzen.
- Du hast im JSON-Vertrag bereits **`tx_commands` reserviert** – der Rückkanal
  ist also schon konzeptionell vorgesehen; wir generalisieren ihn nur.
- Der **Settings-/Info-Screen** (Feature 005) wird für die Diagnose im
  netzbetrieb noch wertvoller (siehe Punkt 4, serieller Monitor).

## 3. Die zentrale Hardware-Erkenntnis (bitte gegenprüfen)

Beim Durcharbeiten deines Codes ist ein **fundamentaler Constraint**
aufgetaucht, der das ganze Konzept prägt:

> **USB und CAN können auf diesem Board nicht gleichzeitig betrieben werden –
> sie teilen sich physisch dieselben Pins.**

Die Beweiskette:

1. Beim **ESP32-S3** liegen die nativen **USB-D-/D+-Leitungen fest verdrahtet
   auf GPIO19/GPIO20** (Silizium, nicht verschiebbar).
2. Dein CAN nutzt **genau diese Pins** – `TWAI_TX=GPIO20`, `TWAI_RX=GPIO19`.
3. Dein eigener Code sagt es sogar explizit –
   `src/hal/waveshare_rgb_lcd_port.c`, `waveshare_rgb_lcd_can_mux_enable()`:
   ```c
   // 0x1E = Backlight-Bits, 0x20 = USB_SEL HIGH → CAN; kombiniert 0x3E
   ```
   Der CH422G schaltet über **`USB_SEL`** die Pins um: **HIGH → CAN**
   (Transceiver), **LOW → USB** (Stecker). Aktuell setzt die Firmware das beim
   Boot fest auf CAN.

**Bitte bestätige oder korrigiere das aus deiner Board-Kenntnis** – das ist der
Angelpunkt des ganzen Designs. Zwei Detailfragen, bei denen deine Erfahrung
Gold wert ist:

- Hängt der USB-C-Stecker am **USB-OTG-Controller** (der die Netzwerkklasse
  kann) oder nur am **USB-Serial-JTAG**?
- Gibt es einen **separaten UART-Bridge-Chip** für den seriellen Monitor – oder
  ist im CAN-Modus (Pins am Transceiver) auch die serielle Konsole weg?

## 4. Die Lösung: umschaltbare Betriebsmodi (USB-first)

Statt gegen den Constraint anzukämpfen, machen wir ihn zu einer expliziten
**Betriebsart**. Wichtig: **Nur USB** kollidiert mit CAN – **WLAN nutzt den
Funk-Teil**, läuft also mit CAN problemlos gleichzeitig.

Drei Personas, beim Boot automatisch anhand „ist ein USB-Host dran?" gewählt:

| Situation beim Boot | Modus | Mux | CAN | Netz |
|---|---|---|---|---|
| USB-Host angeschlossen | **USB-NET** | USB | ❌ | USB-LAN (Gadget) |
| kein USB, WLAN konfiguriert | **WLAN-STA** | CAN | ✅ | externes LAN |
| kein USB, kein WLAN | **WLAN-SoftAP** | CAN | ✅ | eigener AP |

**Boot-Logik:** Beim Start kurz USB-Gadget hochfahren und auf Host-Enumeration
warten (`USB_PROBE_TIMEOUT`, z. B. 3 s). Host da → **USB-NET** (einfachster
Commissioning-Weg am PC). Kein Host → Mux auf CAN, WLAN hoch: konfiguriert →
**STA**, sonst → **SoftAP**. So gilt:

- **Inbetriebnahme:** USB anstecken → sofort USB-Netz + (später) MQTT. Für den
  Techniker der deterministischste Weg, ohne WLAN-Suche.
- **Feld/standalone:** ohne USB läuft das Gerät als **CAN-Display** und stellt
  sein Netz per **eigenem AP** bereit – **CAN hängt nie von einem externen AP
  ab**.
- **Büro/Labor:** ohne USB, mit hinterlegtem WLAN → verbindet sich als STA,
  CAN + MQTT gleichzeitig übers LAN.
- **Kein Aussperren:** USB da → USB-NET erreichbar; kein USB → mindestens
  SoftAP erreichbar.

Moduswechsel erfolgt per **Reboot** (der Mux und die Treiber-Neuinitialisierung
lassen sich nicht sauber im Betrieb umschalten). Der Wunschmodus + WLAN-Zugang
werden in **NVS** persistiert; im Web-Frontend kommt ein **WLAN-Konfig-Formular**
hinzu.

## 5. Das Datenmodell (bewusst einfach gehalten)

- **Lesen:** `signal.source = can | mqtt | sim`
- **Schreiben:** `signal.write = can_tx | mqtt_pub`
- **Kein protokollübergreifendes Routing.** Ein MQTT-Empfang löst nie ein
  CAN-Senden aus (und umgekehrt) – jede Schreibrichtung hat genau eine Sink.
  Nur: `MQTT→Anzeige`, `CAN→Anzeige`, `Bedienung→MQTT-Publish`,
  `Bedienung→CAN-Send`.
- **Werte bleiben numerisch** (`float`). `text-edit`/String-Werte sind bewusst
  im Backlog (kein Treiber in v1).
- **Neue Eingabe-Widgets:** `push-button`, `radio-switch`, `check-button`,
  `numeric-edit`. Ein Widget an einem im aktuellen Modus nicht verfügbaren Bus
  wird einfach **stale** (deine bestehende Stale-Logik greift).

## 6. Der on-device MQTT-Broker

Der Broker läuft **auf dem Gerät** (Autarkie). Die üblichen kleinen
ESP32-Broker (PicoMQTT, sMQTTBroker) sind **C++/Arduino** und passen nicht zur
`C-first`-Regel eurer Constitution. Plan ist daher ein **schlanker, selbst
gebauter Broker in reinem C** (MQTT 3.1.1, QoS 0/1, CONNECT/SUB/PUBLISH-Fan-out)
– die Protokoll-Logik landet als nativ testbares `app/`-Modul
(`mqtt_broker_core`), der Socket-Teil als `hal/`-Modul. Falls du zu diesem
Punkt eine andere Präferenz hast (z. B. eine bewusste Ausnahme für eine
Fremdbibliothek), lass es uns wissen – das ist eine der offeneren Design-Fragen.

## 7. Phasen & nächste Schritte

Wir liefern in drei unabhängig testbaren Phasen, **Hardware/Transport zuerst**
(um genau das USB-OTG-Risiko früh zu klären):

1. **Phase 1 – Betriebsmodus & Netz-Transport** (noch kein MQTT):
   USB-OTG-Spike → USB-Netz-Gadget → WLAN STA+**SoftAP** → Mode-Manager
   (Boot-Probe, NVS, Fallback, Mux modusabhängig) → WLAN-Konfig im Frontend.
   *Ergebnis:* USB anstecken → Editor über USB; ohne USB → CAN läuft, Gerät per
   STA/SoftAP erreichbar.
2. **Phase 2 – Source-Abstraktion + Broker + MQTT-Lesen:** CAN wird hinter die
   `data_source`-Abstraktion gekapselt (Verhalten unverändert), Broker + MQTT
   als Lesequelle.
3. **Phase 3 – Rückkanal:** Schreibpfad (`can_tx` / `mqtt_pub`) + die neuen
   Eingabe-Widgets.

**Was jetzt ansteht:** Für **Phase 1** wird ein detaillierter, task-weiser
Implementierungsplan geschrieben (TDD, wo `app/`-Logik betroffen ist; on-device-
Verifikation für den HAL-Teil). Bevor es an Code geht, ist dein Blick auf
**Abschnitt 3 (Hardware)** am wertvollsten – wenn dort etwas anders ist als
angenommen, ändert das den Zuschnitt von Phase 1.

## 8. Womit du am meisten hilfst

- **Bestätige/korrigiere die Pin-/Mux-Fakten** (Abschnitt 3) und die
  USB-OTG-vs-Serial-JTAG-Frage.
- **Serieller Monitor im CAN-Modus:** separater UART-Bridge-Chip vorhanden?
- **Broker-Sprachfrage** (Abschnitt 6): eigener C-Broker ok, oder Präferenz?
- Generell: alles, was aus Board-/Praxissicht gegen die Betriebsmodus-Idee
  spricht, jetzt statt später.

Danke – und schön, auf so einer sauberen Codebasis aufsetzen zu können.
```

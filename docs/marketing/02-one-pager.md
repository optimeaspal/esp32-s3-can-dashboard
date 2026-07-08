# ESP32-S3 Dashboard
### Das protokoll-agnostische Touch-Dashboard — CAN + MQTT, bidirektional, autark

![Geräte-Mockup](assets/hero-device.svg)

**Ein Display für alle Signale.** CAN-Bus und MQTT werden zu einer gemeinsamen
Anzeige — Werte aufs Display **und** Bedienelemente, die zurücksenden. Was
angezeigt und gesendet wird, steht in einer `dashboard.json`; geändert wird per
Web-Editor und drahtlosem Upload, **nie durch Neu-Kompilieren**.

**Ideal für:** Prüfstände · Maschinen- & Fahrzeugumbauten · Retrofit-Cockpits ·
IoT-/Netzwerk-Anzeigen · Prototyping · Service & Diagnose

---

#### ✔ Warum dieses Dashboard?

- **Protokoll-agnostisch** — die UI kennt nur **Signalnamen**; ob CAN oder MQTT,
  steht in der Signaldefinition, nicht im Widget.
- **Bidirektional** — Anzeige-Widgets zeigen an, Eingabe-Widgets (Taster,
  Schalter, Zahlenfeld) senden per **CAN-Frame** oder **MQTT-Publish** zurück.
- **Autark** — **eigener MQTT-Broker** und **eigenes Netz** an Bord; kein
  externer Broker, kein zwingender Router.
- **Keine Programmierung** — komplette Anzeige über JSON, kein Flashen.
- **Visueller Web-Editor** — Widgets per Maus, gerätegetreue Vorschau; online am
  Gerät **oder** offline im Browser.
- **Drahtloser Upload** — Konfiguration validiert übernehmen, sicher neu starten.
- **Betriebssicher** — Stale-Erkennung graut tote Signale aus, Warnschwellen mit
  eigener Warnfarbe pro Widget.

---

#### 🔀 Automatische Betriebsmodi (USB-first)

![Betriebsmodi](assets/betriebsmodi.svg)

> **USB anstecken → sofort am PC.** Ohne USB läuft das Gerät als **CAN-Display**
> und stellt sein Netz selbst bereit — CAN hängt nie von einem externen AP ab.
> *(WLAN nutzt den Funkteil und läuft mit CAN gleichzeitig; nur USB und CAN
> teilen sich die Pins.)*

---

#### 🔧 Technische Eckdaten

| | |
|---|---|
| **Plattform** | Waveshare ESP32-S3-Touch-LCD-7 · Dual-Core 240 MHz |
| **Display** | 7″ IPS, 800×480, kapazitiver 5-Punkt-Touch (GT911) |
| **Datenquellen** | CAN (TWAI/TJA1051, 25 kBit/s…1 MBit/s) · MQTT · Simulator |
| **Rückkanal** | CAN-Send · MQTT-Publish (MQTT 3.1.1, QoS 0/1, on-device Broker) |
| **Netz** | USB-LAN-Gadget · WLAN-STA · WLAN-SoftAP (mDNS `dashboard.local`) |
| **Widgets** | Gauge · Arc · Chart · Bar · LED · Label · Button · Switch · Edit |
| **Konfiguration** | `dashboard.json` auf SD · Web-Editor · drahtloser Upload |
| **Speicher** | 8 MB Flash · 8 MB PSRAM · SD-Karte (FAT32) |

> **Grafisch. Protokoll-agnostisch. Bidirektional. Autark. Ohne Code.**

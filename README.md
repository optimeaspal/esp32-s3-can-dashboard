# ESP32-S3 CAN Dashboard

Grafisches CAN-Bus-Dashboard auf dem **Waveshare ESP32-S3-Touch-LCD-7** (7" IPS, 800×480).  
LVGL-Widgets (Gauge, Chart, Bar, LED) visualisieren CAN-Signale in Echtzeit.

## Hardware

| Komponente | Detail |
|---|---|
| MCU | ESP32-S3, Dual-Core 240 MHz |
| Display | 7" IPS 800×480, ST7701, RGB-Interface |
| Touch | GT911, kapazitiv, 5-Punkt |
| Flash | 8 MB QIO |
| PSRAM | 8 MB Octal |
| CAN-Transceiver | TJA1051, onboard |
| IO-Expander | CH422G (I²C 0x24/0x38) |
| TF-Karte | vorhanden (SD_CS über CH422G) |

### Pinbelegung CAN

| Signal | GPIO |
|---|---|
| CAN TX | GPIO 20 |
| CAN RX | GPIO 19 |
| I²C SDA (CH422G/GT911) | GPIO 8 |
| I²C SCL (CH422G/GT911) | GPIO 9 |

> **Hinweis:** Der CH422G-Expander schaltet bei CAN-Init den USB_SEL-Pin HIGH –  
> dadurch werden GPIO 19/20 zum TJA1051-Transceiver geroutet (nicht USB).

### RGB-Display-Pins (ST7701, 16-Bit-Bus)

VSYNC=3, HSYNC=46, DE=5, PCLK=7  
DATA0..15 = 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40

## Entwicklungsumgebung

- **PlatformIO** (VS Code Extension)
- **Framework:** ESP-IDF (`framework = espidf`)
- **ESP-IDF Version:** ≥ 5.1.0

## Setup

### 1. Voraussetzungen

- VS Code + PlatformIO Extension installieren
- Git, PlatformIO CLI

### 2. Repository klonen

```bash
git clone git@github.com:optimeaspal/esp32-s3-can-dashboard.git
cd esp32-s3-can-dashboard
```

### 3. Build

```bash
pio run -e esp32-s3-touch-lcd-7
```

Beim ersten Build werden LVGL und der GT911-Treiber automatisch via ESP-IDF Component Manager heruntergeladen.

### 4. Flash & Monitor

Board via USB-C anschließen. Falls nötig: Boot-Modus durch Halten von BOOT während RESET.

```bash
pio run -e esp32-s3-touch-lcd-7 -t upload
pio device monitor
```

### 5. Unit-Tests (kein Board erforderlich)

```bash
pio test -e native
```

Testet die CAN-Signal-Dekodierungslogik nativ auf dem PC (Unity-Framework).

## Konfiguration

Anpassungen ohne Code-Änderung über `sdkconfig.defaults` oder `pio run -t menuconfig`:

| Parameter | Kconfig | Standard |
|---|---|---|
| CAN-Bitrate | `CONFIG_CAN_BITRATE_KBPS` | 500 kBit/s |
| CAN TX GPIO | `CONFIG_EXAMPLE_TX_GPIO_NUM` | 20 |
| CAN RX GPIO | `CONFIG_EXAMPLE_RX_GPIO_NUM` | 19 |
| Signal Stale-Timeout | `CONFIG_CAN_SIGNAL_STALE_MS` | 2000 ms |
| RX Queue-Länge | `CONFIG_CAN_RX_QUEUE_LEN` | 32 |

### CAN-Signale anpassen

Die Signal-Tabelle in [src/main.c](src/main.c) definiert welche CAN-IDs auf welche Widgets gemappt werden:

```c
const can_signal_t can_signals[] = {
    { .can_id=0x101, .byte_offset=0, .byte_length=2, .scale=1.0f, ... },  // → Gauge
    { .can_id=0x102, .byte_offset=0, .byte_length=1, .scale=1.0f, ... },  // → Chart
    { .can_id=0x103, .byte_offset=0, .byte_length=1, .scale=100.0f/255.f }, // → Bar
    { .can_id=0x104, .byte_offset=0, .byte_length=1, .scale=1.0f, ... },  // → LED
};
```

## Architektur

```
app_main()
  │
  ├─ waveshare_rgb_lcd_init()   ← HAL: ST7701-RGB + GT911-Touch + LVGL-Init
  │
  ├─ dashboard_create()         ← UI: LVGL-Screen mit 4 Widgets erstellen
  │
  ├─ lv_timer_create(dashboard_tick, 50ms)   ← UI-Update im LVGL-Task
  │
  └─ can_dispatcher_start()     ← CAN-Task (Core 0)
         │
         ├─ waveshare_twai_init()   ← HAL: CH422G + TWAI-Treiber
         │
         └─ Dispatcher-Loop:
               twai_receive() → can_signal_decode() → xQueueSend(event_queue)
                                                            │
                                              dashboard_tick() ← lv_timer
                                                xQueueReceive()
                                                widget update (lv_meter/chart/bar/led)
                                                stale-check → ausgegraut
```

### Datenmodell

- **`can_signal_t`** (`src/app/can_signal.h`): CAN-ID, Byte-Position, Länge, Endianness, Scale/Offset, Min/Max, Timeout
- **`can_value_event_t`** (`src/app/can_dispatcher.h`): dekodierter Float-Wert + Zeitstempel pro Signal
- **`dashboard_tick()`** konsumiert Events aus der Queue und aktualisiert LVGL-Widgets innerhalb des LVGL-Mutex

## Projektstruktur

```
├── src/
│   ├── Kconfig.projbuild    ← menuconfig-Einträge (Bitrate, GPIOs, Timeouts)
│   ├── main.c               ← Signal-Tabelle, Initialisierungssequenz
│   ├── hal/                 ← Hardware-Porting (ESP-IDF-abhängig)
│   │   ├── lvgl_port.c/h
│   │   ├── waveshare_rgb_lcd_port.c/h
│   │   └── waveshare_twai_port.c/h
│   ├── app/                 ← Anwendungslogik (hardware-unabhängig, testbar)
│   │   ├── can_signal.c/h   ← Signal-Dekodierung
│   │   └── can_dispatcher.c/h
│   └── ui/                  ← LVGL-Dashboard
│       └── dashboard.c/h
├── test/
│   └── test_can_signal.c    ← Unity-Tests (pio test -e native)
├── platformio.ini
├── sdkconfig.defaults
└── .clang-format
```

## Test mit USB-CAN-A Adapter

Zum Testen ohne Fahrzeug-CAN-Bus:  
→ USB-CAN-A-Tool aus `documents/USBCANV2.10.zip` verwenden und Test-Frames einspeisen:

| Widget | CAN-ID | Beispiel-Frame |
|---|---|---|
| Gauge (RPM) | 0x101 | `B8 0B 00 00 00 00 00 00` (= 3000 RPM) |
| Chart (Temp) | 0x102 | `64 00 00 00 00 00 00 00` (= 60°C) |
| Bar (Fuel) | 0x103 | `80 00 00 00 00 00 00 00` (≈ 50%) |
| LED (Warn) | 0x104 | `01 00 00 00 00 00 00 00` (= AN) |

## Roadmap

- [ ] Schritt 2: Hardware-Verifikation (Display, Touch, CAN) mit dem Board
- [ ] JSON-basierte Konfiguration auf TF-Karte (Signal-Tabelle + Widget-Layout)
- [ ] Spec-Kit-Phase: formale Spezifikation der konfigurierbaren Ausbaustufe
- [ ] Mehrere Dashboard-Seiten (Touch-Wischgesten)
- [ ] Helligkeitssteuerung über Touch

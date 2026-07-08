# ESP32-S3 Dashboard — Elevator Pitch

![Geräte-Mockup](assets/hero-device.svg)

## Ein universelles Touch-Dashboard, das man nicht programmieren muss.

Das ESP32-S3 Dashboard macht aus **CAN-Bus und MQTT** ein gemeinsames,
grafisches Live-Display auf einem 7″-Touchscreen — und zwar **bidirektional**:
Werte kommen aufs Display, Bedienelemente am Display senden zurück. Ob ein Wert
per CAN oder per MQTT eintrifft und wohin ein Tastendruck geht, steht in **einer
JSON-Datei** auf der SD-Karte. Die Oberfläche kennt nur noch **Signalnamen** —
kein Neu-Kompilieren, kein Toolchain-Setup, kein Entwickler nötig.

Das Gerät ist **autark**: Es bringt seinen **eigenen MQTT-Broker** und sein
**eigenes Netzwerk** mit — kein externer Broker, kein zwingender Router. Beim
Einschalten wählt es den passenden Betriebsmodus selbst: **USB anstecken** und
sofort am PC konfigurieren, oder im Feld als eigenständiges **CAN-Display** mit
eigenem WLAN-Zugangspunkt. Konfiguriert wird visuell im mitgelieferten
**Web-Editor** (Drag & Drop, gerätegetreue Vorschau) und **drahtlos** hochgeladen
— validiert, bevor es übernommen wird.

Ergebnis: In Minuten von der CAN-ID oder dem MQTT-Topic zum fertigen, bedienbaren
Anzeigefeld — für Prüfstände, Maschinen- und Fahrzeugumbauten, Prototyping und
Serienmaschinen.

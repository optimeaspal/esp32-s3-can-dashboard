#pragma once

/*
 * Hardware-unabhängige CAN-Signal-Abstraktionsschicht.
 * Keine ESP-IDF-Abhängigkeiten → vollständig unit-testbar auf native.
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximale Signalname-/Einheitslänge
#define CAN_SIGNAL_NAME_LEN 32
#define CAN_SIGNAL_UNIT_LEN  8

/*
 * Beschreibt ein einzelnes physikalisches Signal auf dem CAN-Bus.
 *
 * Physikalischer Wert = raw * scale + offset
 * Werte außerhalb [min_value, max_value] werden geclampt.
 */
typedef struct
{
    uint32_t can_id;        // CAN-Nachrichten-ID (11-Bit standard oder 29-Bit extended)
    bool     extended_id;   // true = 29-Bit Extended Frame
    uint8_t  byte_offset;   // Startbyte im Nutzdatenfeld [0..7]
    uint8_t  byte_length;   // Länge in Bytes: 1, 2 oder 4
    bool     little_endian; // true = Intel (LE), false = Motorola (BE)
    bool     is_signed;     // true = vorzeichenbehafteter Rohwert
    bool     is_float;      // true = IEEE-754-Float (scale/offset werden ignoriert)
    bool     is_simulated;  // true = Wert wird vom Simulator erzeugt, nicht vom CAN-Bus

    float    scale;         // Skalierungsfaktor (raw → physikalisch)
    float    offset;        // Offset (raw → physikalisch)
    float    min_value;     // Clamp-Untergrenze für Anzeige
    float    max_value;     // Clamp-Obergrenze für Anzeige
    uint32_t timeout_ms;    // Stale-Timeout: 0 = kein Timeout (Signal immer gültig)

    char name[CAN_SIGNAL_NAME_LEN];
    char unit[CAN_SIGNAL_UNIT_LEN];
} can_signal_t;

/*
 * Dekodiert ein Signal aus einem CAN-Nutzdatenfeld.
 *
 * @param sig   Signal-Deskriptor
 * @param data  Zeiger auf die 8 Datenbytes des CAN-Frames
 * @param dlc   Data Length Code des empfangenen Frames
 *
 * @return Physikalischer Wert (geclampt auf [min_value, max_value]).
 *         Gibt NAN zurück wenn das Signal nicht in den Frame passt (dlc zu kurz).
 */
float can_signal_decode(const can_signal_t *sig, const uint8_t *data, uint8_t dlc);

/*
 * Gibt true zurück wenn id und extended_id zum Signal passen.
 */
static inline bool can_signal_matches(const can_signal_t *sig, uint32_t id, bool extended)
{
    return (sig->can_id == id) && (sig->extended_id == extended);
}

#ifdef __cplusplus
}
#endif

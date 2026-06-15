#include "can_signal.h"
#include <string.h>

/*
 * Extrahiert einen Rohwert aus den CAN-Datenbytes.
 * Unterstützt 1, 2 und 4 Bytes, Little- und Big-Endian, vorzeichenbehaftet und unsigned.
 */
static int32_t extract_raw(const can_signal_t *sig, const uint8_t *data)
{
    uint32_t raw = 0;

    if (sig->little_endian) {
        for (int i = sig->byte_length - 1; i >= 0; i--)
            raw = (raw << 8) | data[sig->byte_offset + i];
    } else {
        for (int i = 0; i < sig->byte_length; i++)
            raw = (raw << 8) | data[sig->byte_offset + i];
    }

    if (sig->is_signed) {
        // Vorzeichenerweiterung auf int32_t
        uint32_t sign_bit = 1u << (sig->byte_length * 8 - 1);
        if (raw & sign_bit)
            return (int32_t)(raw | (~0u << (sig->byte_length * 8)));
    }

    return (int32_t)raw;
}

float can_signal_decode(const can_signal_t *sig, const uint8_t *data, uint8_t dlc)
{
    if (!sig || !data)
        return NAN;
    if (sig->byte_offset + sig->byte_length > dlc)
        return NAN; // Signal passt nicht in den Frame

    float value;

    if (sig->is_float) {
        // IEEE-754-Float: 4 Bytes direkt als float reinterpretieren
        uint32_t raw = 0;
        if (sig->little_endian) {
            for (int i = sig->byte_length - 1; i >= 0; i--)
                raw = (raw << 8) | data[sig->byte_offset + i];
        } else {
            for (int i = 0; i < sig->byte_length; i++)
                raw = (raw << 8) | data[sig->byte_offset + i];
        }
        memcpy(&value, &raw, sizeof(float));
    } else {
        int32_t raw = extract_raw(sig, data);
        value = (float)raw * sig->scale + sig->offset;
    }

    // Auf Anzeigebereich clampen
    if (value < sig->min_value) value = sig->min_value;
    if (value > sig->max_value) value = sig->max_value;

    return value;
}

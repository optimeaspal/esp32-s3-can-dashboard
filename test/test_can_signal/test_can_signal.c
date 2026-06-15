/*
 * Unit-Tests für can_signal_decode().
 * Läuft nativ auf dem PC via: pio test -e native
 *
 * Framework: Unity (in PlatformIO integriert)
 */
#include <math.h>
#include <stdint.h>
#include "unity.h"

// Direkte Include-Pfad-Anpassung für native Tests
#include "../../src/app/can_signal.h"
#include "../../src/app/can_signal.c"

void setUp(void) {}
void tearDown(void) {}

// ── Hilfsmakro ───────────────────────────────────────────────────────────────
#define ASSERT_FLOAT_EQ(expected, actual) \
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (expected), (actual))

// ── uint8, Little-Endian, scale=1, offset=0 ──────────────────────────────────
void test_uint8_raw_value(void)
{
    can_signal_t sig = {
        .can_id       = 0x100,
        .byte_offset  = 0,
        .byte_length  = 1,
        .little_endian = true,
        .is_signed    = false,
        .scale        = 1.0f,
        .offset       = 0.0f,
        .min_value    = 0.0f,
        .max_value    = 255.0f,
    };
    uint8_t data[8] = {0xA0};
    float result = can_signal_decode(&sig, data, 8);
    ASSERT_FLOAT_EQ(160.0f, result);
}

// ── uint16, Little-Endian, scale=1, Drehzahl-Simulation ─────────────────────
void test_uint16_le_rpm(void)
{
    can_signal_t sig = {
        .can_id       = 0x101,
        .byte_offset  = 0,
        .byte_length  = 2,
        .little_endian = true,
        .is_signed    = false,
        .scale        = 1.0f,
        .offset       = 0.0f,
        .min_value    = 0.0f,
        .max_value    = 6000.0f,
    };
    // 3000 RPM → 0x0BB8 → LE: [0xB8, 0x0B]
    uint8_t data[8] = {0xB8, 0x0B};
    float result = can_signal_decode(&sig, data, 8);
    ASSERT_FLOAT_EQ(3000.0f, result);
}

// ── uint16, Big-Endian, scale=0.1 → 40.0 ────────────────────────────────────
void test_uint16_be_scale(void)
{
    can_signal_t sig = {
        .can_id       = 0x200,
        .byte_offset  = 2,
        .byte_length  = 2,
        .little_endian = false, // Big-Endian
        .is_signed    = false,
        .scale        = 0.1f,
        .offset       = 0.0f,
        .min_value    = 0.0f,
        .max_value    = 100.0f,
    };
    // raw = 400 → 0x0190, BE: [0x01, 0x90]
    uint8_t data[8] = {0, 0, 0x01, 0x90};
    float result = can_signal_decode(&sig, data, 8);
    ASSERT_FLOAT_EQ(40.0f, result);
}

// ── int8, vorzeichenbehaftet, offset=-40 (Temperatur-Codierung) ──────────────
void test_int8_signed_with_offset(void)
{
    can_signal_t sig = {
        .can_id       = 0x102,
        .byte_offset  = 0,
        .byte_length  = 1,
        .little_endian = true,
        .is_signed    = true,
        .scale        = 1.0f,
        .offset       = -40.0f,
        .min_value    = -40.0f,
        .max_value    = 150.0f,
    };
    // raw = 100 → physikalisch: 100 - 40 = 60°C
    uint8_t data[8] = {100};
    float result = can_signal_decode(&sig, data, 8);
    ASSERT_FLOAT_EQ(60.0f, result);
}

// ── Negativer int8-Wert ───────────────────────────────────────────────────────
void test_int8_negative(void)
{
    can_signal_t sig = {
        .can_id       = 0x102,
        .byte_offset  = 0,
        .byte_length  = 1,
        .little_endian = true,
        .is_signed    = true,
        .scale        = 1.0f,
        .offset       = -40.0f,
        .min_value    = -40.0f,
        .max_value    = 150.0f,
    };
    // raw = -10 (= 0xF6) → physikalisch: -10 - 40 = -50 → clamped auf -40
    uint8_t data[8] = {0xF6};
    float result = can_signal_decode(&sig, data, 8);
    ASSERT_FLOAT_EQ(-40.0f, result); // clamped
}

// ── Clamp: Wert oberhalb max_value ───────────────────────────────────────────
void test_clamp_max(void)
{
    can_signal_t sig = {
        .can_id       = 0x101,
        .byte_offset  = 0,
        .byte_length  = 2,
        .little_endian = true,
        .is_signed    = false,
        .scale        = 1.0f,
        .offset       = 0.0f,
        .min_value    = 0.0f,
        .max_value    = 6000.0f,
    };
    // raw = 65535 → 6000 (max)
    uint8_t data[8] = {0xFF, 0xFF};
    float result = can_signal_decode(&sig, data, 8);
    ASSERT_FLOAT_EQ(6000.0f, result);
}

// ── DLC zu kurz: Signal passt nicht in Frame → NAN ───────────────────────────
void test_nan_if_frame_too_short(void)
{
    can_signal_t sig = {
        .can_id       = 0x100,
        .byte_offset  = 6,
        .byte_length  = 2, // würde Bytes 6+7 benötigen
        .little_endian = true,
        .is_signed    = false,
        .scale        = 1.0f,
        .offset       = 0.0f,
        .min_value    = 0.0f,
        .max_value    = 255.0f,
    };
    uint8_t data[8] = {0};
    float result = can_signal_decode(&sig, data, 7); // DLC = 7, Byte 7 fehlt
    TEST_ASSERT_TRUE(isnan(result));
}

// ── NULL-Pointer-Schutz ───────────────────────────────────────────────────────
void test_null_returns_nan(void)
{
    uint8_t data[8] = {0};
    TEST_ASSERT_TRUE(isnan(can_signal_decode(NULL, data, 8)));
    can_signal_t sig = {0};
    TEST_ASSERT_TRUE(isnan(can_signal_decode(&sig, NULL, 8)));
}

// ── Scale 100/255 – Kraftstoff-Codierung ─────────────────────────────────────
void test_fuel_scale(void)
{
    can_signal_t sig = {
        .can_id       = 0x103,
        .byte_offset  = 0,
        .byte_length  = 1,
        .little_endian = true,
        .is_signed    = false,
        .scale        = 100.0f / 255.0f,
        .offset       = 0.0f,
        .min_value    = 0.0f,
        .max_value    = 100.0f,
    };
    uint8_t data[8] = {128}; // Halbvoll
    float result = can_signal_decode(&sig, data, 8);
    // 128 * (100/255) ≈ 50.2%
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 50.0f, result);
}

// ── Testrunner ────────────────────────────────────────────────────────────────
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uint8_raw_value);
    RUN_TEST(test_uint16_le_rpm);
    RUN_TEST(test_uint16_be_scale);
    RUN_TEST(test_int8_signed_with_offset);
    RUN_TEST(test_int8_negative);
    RUN_TEST(test_clamp_max);
    RUN_TEST(test_nan_if_frame_too_short);
    RUN_TEST(test_null_returns_nan);
    RUN_TEST(test_fuel_scale);
    return UNITY_END();
}

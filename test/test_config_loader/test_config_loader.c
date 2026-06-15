/*
 * Unit-Tests für config_loader_parse().
 * Native: pio test -e native
 *
 * Lädt JSON-Fixtures aus test/fixtures/ (Arbeitsverzeichnis = Projektwurzel)
 * und prüft Parsing, Signal-Binding-Auflösung, Fehlerbehandlung und Farb-Parsing.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"

#include "../../src/app/config_loader.h"
#include "../../src/app/config_loader.c"

void setUp(void) {}
void tearDown(void) {}

static dashboard_config_t g_cfg;
static char               g_err[160];

// Liest eine Fixture-Datei in einen statischen Puffer. Versucht mehrere
// Pfad-Präfixe, da das Arbeitsverzeichnis je nach Runner variieren kann.
static const char *load_fixture(const char *name)
{
    static char buf[16384];
    const char *prefixes[] = {
        "test/fixtures/", "../../test/fixtures/", "fixtures/", "./test/fixtures/"
    };
    for (size_t p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]); p++) {
        char path[256];
        snprintf(path, sizeof(path), "%s%s", prefixes[p], name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        return buf;
    }
    TEST_FAIL_MESSAGE("Fixture nicht gefunden – Arbeitsverzeichnis prüfen");
    return NULL;
}

// ── Valide 1-Seiten-Konfiguration ─────────────────────────────────────────────
void test_parse_valid_1page(void)
{
    const char *json = load_fixture("valid_2widget_1page.json");
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    TEST_ASSERT_EQUAL_UINT8(2, g_cfg.signal_count);
    TEST_ASSERT_EQUAL_UINT8(1, g_cfg.page_count);
    TEST_ASSERT_EQUAL_UINT8(2, g_cfg.pages[0].widget_count);
}

// ── Valide 2-Seiten-Konfiguration ─────────────────────────────────────────────
void test_parse_valid_2pages(void)
{
    const char *json = load_fixture("valid_8widget_2page.json");
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    TEST_ASSERT_EQUAL_UINT8(2, g_cfg.page_count);
    TEST_ASSERT_TRUE(g_cfg.has_tx_commands); // "tx_commands":[] ist vorhanden
}

// ── Syntaxfehler → INVALID_ARG ────────────────────────────────────────────────
void test_parse_invalid_syntax(void)
{
    const char *json = load_fixture("invalid_syntax.json");
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, rc);
}

// ── Fehlendes Pflichtfeld (byte_length) → INVALID_ARG ─────────────────────────
void test_parse_missing_field(void)
{
    const char *json = load_fixture("missing_required_field.json");
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, rc);
}

// ── Signal-Binding wird korrekt aufgelöst ─────────────────────────────────────
void test_signal_binding_resolve(void)
{
    const char *json = load_fixture("valid_2widget_1page.json");
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    // Widget 0 → "RPM" (Signal 0), Widget 1 → "Temperatur" (Signal 1)
    TEST_ASSERT_EQUAL_INT(0, g_cfg.pages[0].widgets[0].signal_idx);
    TEST_ASSERT_EQUAL_INT(1, g_cfg.pages[0].widgets[1].signal_idx);
}

// ── Unbekannte Signal-Referenz → NOT_FOUND ────────────────────────────────────
void test_signal_binding_unknown(void)
{
    const char *json =
        "{\"version\":\"1.0\","
        " \"signals\":[{\"name\":\"RPM\",\"can_id\":\"0x100\",\"byte_offset\":0,"
        "   \"byte_length\":2,\"endianness\":\"little\",\"min\":0,\"max\":10}],"
        " \"pages\":[{\"widgets\":[{\"type\":\"gauge\",\"x\":0,\"y\":0,"
        "   \"width\":100,\"height\":100,\"signal\":\"GHOST\"}]}]}";
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, rc);
}

// ── Hex-Farbe "#00AA00" → 0x00AA00 ────────────────────────────────────────────
void test_color_parse_hex(void)
{
    const char *json = load_fixture("valid_2widget_1page.json");
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    // Gauge normal_color "#00AA00"
    TEST_ASSERT_EQUAL_HEX32(0x00AA00, g_cfg.pages[0].widgets[0].style.normal_color);
    // Gauge warning_color "#FF4400"
    TEST_ASSERT_EQUAL_HEX32(0xFF4400, g_cfg.pages[0].widgets[0].style.warning_color);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_valid_1page);
    RUN_TEST(test_parse_valid_2pages);
    RUN_TEST(test_parse_invalid_syntax);
    RUN_TEST(test_parse_missing_field);
    RUN_TEST(test_signal_binding_resolve);
    RUN_TEST(test_signal_binding_unknown);
    RUN_TEST(test_color_parse_hex);
    return UNITY_END();
}

/*
 * Unit-Tests für Entity-Invarianten der Dashboard-Konfiguration.
 * Native: pio test -e native
 *
 * Prüft die semantischen Validierungsregeln aus data-model.md, die
 * config_loader_parse() durchsetzt:
 *   - min < max
 *   - Signalnamen eindeutig
 *   - Widget-Anzahl pro Seite <= CFG_MAX_WIDGETS_PER_PAGE
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"

#include "../../src/app/config_loader.h"
#include "../../src/app/config_loader.c"

void setUp(void) {}
void tearDown(void) {}

static dashboard_config_t g_cfg;
static char               g_err[160];

// ── min muss < max sein ───────────────────────────────────────────────────────
void test_min_less_than_max(void)
{
    const char *json =
        "{\"version\":\"1.0\","
        " \"signals\":[{\"name\":\"S\",\"can_id\":\"0x100\","
        "   \"byte_offset\":0,\"byte_length\":2,\"endianness\":\"little\","
        "   \"min\":100.0,\"max\":10.0}],"   // min >= max → ungültig
        " \"pages\":[{\"widgets\":[{\"type\":\"gauge\",\"x\":0,\"y\":0,"
        "   \"width\":100,\"height\":100,\"signal\":\"S\"}]}]}";
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, rc);
}

// ── Signalnamen müssen eindeutig sein ─────────────────────────────────────────
void test_signal_name_unique(void)
{
    const char *json =
        "{\"version\":\"1.0\","
        " \"signals\":["
        "   {\"name\":\"DUP\",\"can_id\":\"0x100\",\"byte_offset\":0,"
        "    \"byte_length\":2,\"endianness\":\"little\",\"min\":0,\"max\":10},"
        "   {\"name\":\"DUP\",\"can_id\":\"0x101\",\"byte_offset\":0,"
        "    \"byte_length\":2,\"endianness\":\"little\",\"min\":0,\"max\":10}],"
        " \"pages\":[{\"widgets\":[{\"type\":\"gauge\",\"x\":0,\"y\":0,"
        "   \"width\":100,\"height\":100,\"signal\":\"DUP\"}]}]}";
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, rc);
}

// ── Zwei unterschiedliche Namen sind erlaubt (Gegentest) ──────────────────────
void test_two_distinct_names_ok(void)
{
    const char *json =
        "{\"version\":\"1.0\","
        " \"signals\":["
        "   {\"name\":\"A\",\"can_id\":\"0x100\",\"byte_offset\":0,"
        "    \"byte_length\":2,\"endianness\":\"little\",\"min\":0,\"max\":10},"
        "   {\"name\":\"B\",\"can_id\":\"0x101\",\"byte_offset\":0,"
        "    \"byte_length\":2,\"endianness\":\"little\",\"min\":0,\"max\":10}],"
        " \"pages\":[{\"widgets\":[{\"type\":\"gauge\",\"x\":0,\"y\":0,"
        "   \"width\":100,\"height\":100,\"signal\":\"A\"}]}]}";
    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_OK, rc);
    TEST_ASSERT_EQUAL_UINT8(2, g_cfg.signal_count);
}

// ── Widget-Anzahl pro Seite begrenzt ──────────────────────────────────────────
void test_widget_count_limit(void)
{
    // JSON mit (CFG_MAX_WIDGETS_PER_PAGE + 1) Widgets auf einer Seite erzeugen
    static char json[8192];
    int n = 0;
    n += snprintf(json + n, sizeof(json) - n,
        "{\"version\":\"1.0\","
        " \"signals\":[{\"name\":\"S\",\"can_id\":\"0x100\",\"byte_offset\":0,"
        "   \"byte_length\":2,\"endianness\":\"little\",\"min\":0,\"max\":10}],"
        " \"pages\":[{\"widgets\":[");
    for (int i = 0; i < CFG_MAX_WIDGETS_PER_PAGE + 1; i++) {
        n += snprintf(json + n, sizeof(json) - n,
            "%s{\"type\":\"gauge\",\"x\":0,\"y\":0,\"width\":50,\"height\":50,"
            "\"signal\":\"S\"}", (i == 0) ? "" : ",");
    }
    n += snprintf(json + n, sizeof(json) - n, "]}]}");

    esp_err_t rc = config_loader_parse(json, &g_cfg, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_SIZE, rc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_min_less_than_max);
    RUN_TEST(test_signal_name_unique);
    RUN_TEST(test_two_distinct_names_ok);
    RUN_TEST(test_widget_count_limit);
    return UNITY_END();
}

/*
 * Unit-Tests für wifi_credentials_parse().  Native: pio test -e native
 * Fixtures aus test/fixtures/ (gleicher Lade-Mechanismus wie test_config_loader).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"

#include "../../src/app/wifi_credentials.h"
#include "../../src/app/wifi_credentials.c"

void setUp(void) {}
void tearDown(void) {}

static wifi_credentials_t g_wc;
static char               g_err[160];

static const char *load_fixture(const char *name)
{
    static char buf[8192];
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

void test_parse_valid(void)
{
    const char *json = load_fixture("wifi_valid.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    TEST_ASSERT_EQUAL_UINT8(2, g_wc.network_count);
    TEST_ASSERT_EQUAL_STRING("dashboard", g_wc.hostname);
    TEST_ASSERT_EQUAL_STRING("Werkstatt-WLAN", g_wc.networks[0].ssid);
    TEST_ASSERT_EQUAL_STRING("geheim123", g_wc.networks[0].password);
    TEST_ASSERT_EQUAL_STRING("Hotspot-Patrick", g_wc.networks[1].ssid);
}

void test_open_network_has_empty_password(void)
{
    const char *json = load_fixture("wifi_open_network.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    TEST_ASSERT_EQUAL_UINT8(1, g_wc.network_count);
    TEST_ASSERT_EQUAL_STRING("", g_wc.networks[0].password);
}

void test_default_hostname_when_missing(void)
{
    const char *json = load_fixture("wifi_no_hostname.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    TEST_ASSERT_EQUAL_STRING("dashboard", g_wc.hostname);
}

void test_empty_networks_is_error(void)
{
    const char *json = load_fixture("wifi_empty.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, rc);
}

void test_invalid_syntax_is_error(void)
{
    const char *json = load_fixture("wifi_invalid_syntax.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, rc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_valid);
    RUN_TEST(test_open_network_has_empty_password);
    RUN_TEST(test_default_hostname_when_missing);
    RUN_TEST(test_empty_networks_is_error);
    RUN_TEST(test_invalid_syntax_is_error);
    return UNITY_END();
}

#pragma once

/*
 * Minimaler esp_err_t-Stub für native Unit-Tests (pio test -e native).
 *
 * Auf dem Zielgerät (ESP-IDF-Build) wird stattdessen der echte <esp_err.h>
 * verwendet. config_loader.h wählt anhand des Build-Guards ESP_PLATFORM,
 * welcher Header eingebunden wird.
 *
 * Die Zahlenwerte entsprechen den ESP-IDF-Definitionen, damit Tests, die
 * konkrete Codes prüfen, sich identisch zur Hardware verhalten.
 */

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK                  0
#define ESP_FAIL               -1

#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_STATE   0x103
#define ESP_ERR_INVALID_SIZE    0x104
#define ESP_ERR_NOT_FOUND       0x105

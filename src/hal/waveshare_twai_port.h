#pragma once

#include "esp_err.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Pins: TJA1051-Transceiver bereits on-board verdrahtet
#define TWAI_TX_GPIO CONFIG_EXAMPLE_TX_GPIO_NUM
#define TWAI_RX_GPIO CONFIG_EXAMPLE_RX_GPIO_NUM

// I2C CH422G (USB_SEL muss HIGH gesetzt werden damit GPIO19/20 → CAN, nicht USB)
#define TWAI_I2C_NUM        0
#define TWAI_I2C_SDA        8
#define TWAI_I2C_SCL        9
#define TWAI_I2C_TIMEOUT_MS 1000

/*
 * Initialisiert den TWAI-Treiber:
 *   - CH422G via I²C konfigurieren (USB_SEL HIGH → GPIO19/20 → CAN-Transceiver)
 *   - TWAI-Treiber installieren und starten
 *   - Alerts konfigurieren (RX_DATA, ERR_PASS, BUS_ERROR, RX_QUEUE_FULL)
 *
 * @param bitrate_kbps  CAN-Bitrate: 25, 50, 100, 125, 250, 500, 800 oder 1000
 */
esp_err_t waveshare_twai_init(uint32_t bitrate_kbps);

/* Stoppt und deinstalliert den TWAI-Treiber. */
esp_err_t waveshare_twai_deinit(void);

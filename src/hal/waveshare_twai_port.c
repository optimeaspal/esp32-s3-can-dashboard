#include "waveshare_twai_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "twai_port";

static esp_err_t twai_timing_for_kbps(uint32_t kbps, twai_timing_config_t *out)
{
    switch (kbps) {
    case 25:   *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_25KBITS();   break;
    case 50:   *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS();   break;
    case 100:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS();  break;
    case 125:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();  break;
    case 250:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();  break;
    case 500:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();  break;
    case 800:  *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS();  break;
    case 1000: *out = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();    break;
    default:
        ESP_LOGE(TAG, "Ungültige CAN-Bitrate: %" PRIu32 " kBit/s", kbps);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t waveshare_twai_init(uint32_t bitrate_kbps)
{
    twai_timing_config_t t_cfg;
    ESP_ERROR_CHECK(twai_timing_for_kbps(bitrate_kbps, &t_cfg));

    const twai_filter_config_t f_cfg  = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT(
        TWAI_TX_GPIO, TWAI_RX_GPIO, TWAI_MODE_NORMAL);
    g_cfg.rx_queue_len = 128;

    if (twai_driver_install(&g_cfg, &t_cfg, &f_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "TWAI driver install failed");
        return ESP_FAIL;
    }
    if (twai_start() != ESP_OK) {
        ESP_LOGE(TAG, "TWAI driver start failed");
        return ESP_FAIL;
    }

    uint32_t alerts = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS
                    | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL;
    if (twai_reconfigure_alerts(alerts, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "TWAI alert config failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TWAI ready @ %" PRIu32 " kBit/s, TX=%d RX=%d",
             bitrate_kbps, TWAI_TX_GPIO, TWAI_RX_GPIO);
    return ESP_OK;
}

esp_err_t waveshare_twai_deinit(void)
{
    twai_stop();
    return twai_driver_uninstall();
}

#include "waveshare_twai_port.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "twai_port";

static esp_err_t i2c_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TWAI_I2C_SDA,
        .scl_io_num       = TWAI_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(TWAI_I2C_NUM, &conf);
    return i2c_driver_install(TWAI_I2C_NUM, conf.mode, 0, 0, 0);
}

// CH422G: USB_SEL HIGH → GPIO19/20 zu CAN-Transceiver (nicht USB)
static void ch422g_set_usb_sel_high(void)
{
    const TickType_t t = pdMS_TO_TICKS(TWAI_I2C_TIMEOUT_MS);
    uint8_t buf = 0x01;
    i2c_master_write_to_device(TWAI_I2C_NUM, 0x24, &buf, 1, t); // Output-Modus
    buf = 0x20;
    i2c_master_write_to_device(TWAI_I2C_NUM, 0x38, &buf, 1, t); // USB_SEL = HIGH
}

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
    ESP_ERROR_CHECK(i2c_init());
    ch422g_set_usb_sel_high();

    twai_timing_config_t t_cfg;
    ESP_ERROR_CHECK(twai_timing_for_kbps(bitrate_kbps, &t_cfg));

    const twai_filter_config_t f_cfg  = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    const twai_general_config_t g_cfg = TWAI_GENERAL_CONFIG_DEFAULT(
        TWAI_TX_GPIO, TWAI_RX_GPIO, TWAI_MODE_LISTEN_ONLY);

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

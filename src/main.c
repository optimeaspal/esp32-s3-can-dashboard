#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 CAN Dashboard – booting...");

    // Schritt 2: Display-Bringup (waveshare_rgb_lcd_port + lvgl_port)
    // Schritt 3: CAN/TWAI-Bringup
    // Schritt 4: Dashboard-Widgets
}

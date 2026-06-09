#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "lcd_port";

IRAM_ATTR static bool rgb_lcd_on_vsync_event(
    esp_lcd_panel_handle_t panel,
    const esp_lcd_rgb_panel_event_data_t *edata,
    void *user_ctx)
{
    return lvgl_port_notify_rgb_vsync();
}

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911

static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA_IO,
        .scl_io_num       = I2C_MASTER_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static void gpio_touch_ctrl_init(void)
{
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .pin_bit_mask = GPIO_TOUCH_CTRL_SEL,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
}

// CH422G-gesteuerte Touch-Reset-Sequenz (Waveshare-spezifisch)
static void touch_reset(void)
{
    uint8_t buf;
    const TickType_t timeout = I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS;

    buf = 0x01;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x24, &buf, 1, timeout);

    buf = 0x2C;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &buf, 1, timeout);
    esp_rom_delay_us(100 * 1000);

    gpio_set_level(GPIO_TOUCH_CTRL_PIN, 0);
    esp_rom_delay_us(100 * 1000);

    buf = 0x2E;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &buf, 1, timeout);
    esp_rom_delay_us(200 * 1000);
}

#endif /* CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911 */

esp_err_t waveshare_rgb_lcd_init(void)
{
    ESP_LOGI(TAG, "Init RGB LCD panel (ST7701, 800x480)");

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz          = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res            = EXAMPLE_LCD_H_RES,
            .v_res            = EXAMPLE_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch  = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch  = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = 1,
        },
        .data_width           = EXAMPLE_RGB_DATA_WIDTH,
        .bits_per_pixel       = EXAMPLE_RGB_BIT_PER_PIXEL,
        .num_fbs              = LVGL_PORT_LCD_RGB_BUFFER_NUMS,
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE,
        .sram_trans_align     = 4,
        .psram_trans_align    = 64,
        .hsync_gpio_num       = EXAMPLE_LCD_IO_RGB_HSYNC,
        .vsync_gpio_num       = EXAMPLE_LCD_IO_RGB_VSYNC,
        .de_gpio_num          = EXAMPLE_LCD_IO_RGB_DE,
        .pclk_gpio_num        = EXAMPLE_LCD_IO_RGB_PCLK,
        .disp_gpio_num        = EXAMPLE_LCD_IO_RGB_DISP,
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA0,  EXAMPLE_LCD_IO_RGB_DATA1,
            EXAMPLE_LCD_IO_RGB_DATA2,  EXAMPLE_LCD_IO_RGB_DATA3,
            EXAMPLE_LCD_IO_RGB_DATA4,  EXAMPLE_LCD_IO_RGB_DATA5,
            EXAMPLE_LCD_IO_RGB_DATA6,  EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8,  EXAMPLE_LCD_IO_RGB_DATA9,
            EXAMPLE_LCD_IO_RGB_DATA10, EXAMPLE_LCD_IO_RGB_DATA11,
            EXAMPLE_LCD_IO_RGB_DATA12, EXAMPLE_LCD_IO_RGB_DATA13,
            EXAMPLE_LCD_IO_RGB_DATA14, EXAMPLE_LCD_IO_RGB_DATA15,
        },
        .flags.fb_in_psram = 1,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    esp_lcd_touch_handle_t tp_handle = NULL;

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
    ESP_LOGI(TAG, "Init I2C + GT911 touch controller");
    ESP_ERROR_CHECK(i2c_master_init());
    gpio_touch_ctrl_init();
    touch_reset();

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = 0; // Legacy-I2C-Treiber: Geschwindigkeit via i2c_param_config, nicht hier
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)I2C_MASTER_NUM, &tp_io_cfg, &tp_io_handle));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels.reset = 0,
        .levels.interrupt = 0,
        .flags.swap_xy   = 0,
        .flags.mirror_x  = 0,
        .flags.mirror_y  = 0,
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle));
#endif

    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));

    esp_lcd_rgb_panel_event_callbacks_t cbs = {
#if EXAMPLE_RGB_BOUNCE_BUFFER_SIZE > 0
        .on_bounce_frame_finish = rgb_lcd_on_vsync_event,
#else
        .on_vsync = rgb_lcd_on_vsync_event,
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL));

    return ESP_OK;
}

// Backlight über CH422G-Expander einschalten
esp_err_t waveshare_rgb_lcd_bl_on(void)
{
    const TickType_t t = I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS;
    uint8_t buf = 0x01;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x24, &buf, 1, t);
    buf = 0x1E;
    return i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &buf, 1, t);
}

esp_err_t waveshare_rgb_lcd_bl_off(void)
{
    const TickType_t t = I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS;
    uint8_t buf = 0x01;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x24, &buf, 1, t);
    buf = 0x1A;
    return i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &buf, 1, t);
}

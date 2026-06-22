#include "waveshare_sd_port.h"

#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

static const char *TAG = "waveshare_sd";

/* ── Pin-Konfiguration (Kconfig) ──────────────────────────────────────────── */
#define SD_MOSI_GPIO   CONFIG_SD_SPI_MOSI_GPIO   /* 11 */
#define SD_MISO_GPIO   CONFIG_SD_SPI_MISO_GPIO   /* 13 */
#define SD_CLK_GPIO    CONFIG_SD_SPI_CLK_GPIO    /* 12 */
#define SD_SPI_HOST    SPI2_HOST
#define SD_MOUNT_POINT "/sdcard"

/*
 * CH422G-IO-Expander (gemeinsamer I2C-Bus, von waveshare_rgb_lcd_init installiert).
 *   - Adresse 0x24: Konfigurationsregister (Befehl 0x01 = Ausgänge aktiv)
 *   - Adresse 0x38: Ausgangsregister (jeweils 1 Bit pro EXIO-Pin)
 *
 * Basiswert 0x3E entspricht dem Zustand nach waveshare_rgb_lcd_can_mux_enable()
 * (Backlight-Bits + USB_SEL). SD_CS liegt auf CH422G-Pin 4 (0x10), aktiv-LOW:
 *   CS aktiv (low)   → Bit 4 löschen → 0x2E
 *   CS inaktiv (high)→ Bit 4 setzen  → 0x3E
 */
#define CH422G_I2C_NUM      0          /* == I2C_MASTER_NUM in waveshare_rgb_lcd_port */
#define CH422G_CFG_ADDR     0x24
#define CH422G_OUT_ADDR     0x38
#define CH422G_OUT_BASE     0x3E
#define SD_CS_BIT           (1u << CONFIG_SD_CS_CH422G_PIN)  /* Pin 4 → 0x10 */

static esp_err_t ch422g_write_out(uint8_t value)
{
    const TickType_t t = pdMS_TO_TICKS(1000);
    uint8_t cfg = 0x01;
    esp_err_t rc = i2c_master_write_to_device(CH422G_I2C_NUM, CH422G_CFG_ADDR, &cfg, 1, t);
    if (rc != ESP_OK) return rc;
    return i2c_master_write_to_device(CH422G_I2C_NUM, CH422G_OUT_ADDR, &value, 1, t);
}

static esp_err_t sd_cs_assert(bool active)
{
    /* aktiv-LOW: active=true → Bit löschen */
    uint8_t out = active ? (CH422G_OUT_BASE & ~SD_CS_BIT) : (CH422G_OUT_BASE | SD_CS_BIT);
    return ch422g_write_out(out);
}

static sdmmc_card_t *s_card    = NULL;
static bool          s_mounted = false;

esp_err_t waveshare_sd_port_init(void)
{
    ESP_LOGI(TAG, "Init SD (SPI%d: MOSI=%d MISO=%d CLK=%d, CS via CH422G Pin %d)",
             SD_SPI_HOST, SD_MOSI_GPIO, SD_MISO_GPIO, SD_CLK_GPIO,
             CONFIG_SD_CS_CH422G_PIN);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = SD_MOSI_GPIO,
        .miso_io_num     = SD_MISO_GPIO,
        .sclk_io_num     = SD_CLK_GPIO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t rc = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(rc));
        return rc;
    }

    /* SD-CS dauerhaft aktivieren – SD ist einziges Gerät auf diesem Bus.
     * (I2C im SPI-pre/post-Callback ist nicht möglich, daher Session-CS.) */
    rc = sd_cs_assert(true);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "CH422G SD_CS assert fehlgeschlagen: %s", esp_err_to_name(rc));
        return rc;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SD_SPI_HOST;
    slot.gpio_cs = GPIO_NUM_NC;   /* kein direkter GPIO-CS – CH422G steuert CS */

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    rc = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (rc != ESP_OK) {
        if (rc == ESP_FAIL)
            ESP_LOGE(TAG, "FATFS-Mount fehlgeschlagen (Dateisystem?)");
        else
            ESP_LOGE(TAG, "SD-Karte nicht initialisiert: %s", esp_err_to_name(rc));
        return (rc == ESP_FAIL) ? ESP_ERR_NOT_FOUND : rc;
    }

    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD-Karte gemountet unter %s", SD_MOUNT_POINT);
    s_mounted = true;
    return ESP_OK;
}

esp_err_t waveshare_sd_read_file(const char *path, char *buf,
                                 size_t max_len, size_t *out_len)
{
    if (!path || !buf || max_len == 0) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Datei nicht gefunden: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0 || (size_t)size > max_len - 1) {
        fclose(f);
        ESP_LOGE(TAG, "Datei zu groß (%ld B > %zu B Puffer): %s", size, max_len - 1, path);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;

    ESP_LOGI(TAG, "%zu Bytes aus %s gelesen", n, path);
    return ESP_OK;
}

esp_err_t waveshare_sd_write_file(const char *path, const char *buf, size_t len)
{
    if (!path || !buf) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Kann %s nicht zum Schreiben öffnen", path);
        return ESP_FAIL;
    }
    size_t written = fwrite(buf, 1, len, f);
    fclose(f);
    if (written != len) {
        ESP_LOGE(TAG, "Schreibfehler %s (%u/%u Bytes)",
                 path, (unsigned)written, (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t waveshare_sd_rename(const char *from_path, const char *to_path)
{
    remove(to_path);  /* FATFS: rename schlägt fehl, wenn Ziel existiert */
    if (rename(from_path, to_path) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s fehlgeschlagen", from_path, to_path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool waveshare_sd_port_is_mounted(void) { return s_mounted; }

bool waveshare_sd_port_file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

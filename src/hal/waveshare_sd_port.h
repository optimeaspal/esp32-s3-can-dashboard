#pragma once

/*
 * HAL für die SD-Karte des Waveshare ESP32-S3-Touch-LCD-7.
 *
 * Besonderheit: SD-CS wird nicht über einen direkten GPIO, sondern über den
 * CH422G-IO-Expander gesteuert. Siehe specs/001-json-config-dashboard/research.md.
 *
 * waveshare_sd_port_init() MUSS nach waveshare_rgb_lcd_init() aufgerufen werden,
 * damit der CH422G bereits initialisiert ist.
 */

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialisiert SPI-Bus + FATFS und mountet die SD-Karte unter "/sdcard".
 *
 * @return ESP_OK bei Erfolg, sonst ein ESP-IDF-Fehlercode
 *         (z. B. ESP_ERR_NOT_FOUND wenn keine Karte erkannt wurde).
 */
esp_err_t waveshare_sd_port_init(void);

/*
 * Liest eine Datei vollständig in einen vom Aufrufer bereitgestellten Puffer.
 * Der Puffer wird NUL-terminiert (daher max_len inkl. Platz für '\0').
 *
 * @param path     Pfad inkl. Mountpunkt, z. B. "/sdcard/dashboard.json"
 * @param buf      Zielpuffer
 * @param max_len  Größe des Zielpuffers in Bytes
 * @param out_len  (optional) gelesene Bytezahl ohne '\0'; darf NULL sein
 *
 * @return ESP_OK bei Erfolg,
 *         ESP_ERR_NOT_FOUND wenn die Datei nicht existiert,
 *         ESP_ERR_INVALID_SIZE wenn die Datei größer als max_len-1 ist.
 */
esp_err_t waveshare_sd_read_file(const char *path, char *buf,
                                 size_t max_len, size_t *out_len);

/*
 * Schreibt buf (len Bytes) vollständig in eine Datei (überschreibt vorhandene).
 *
 * @return ESP_OK bei Erfolg, ESP_FAIL bei Schreib-/Öffnen-Fehler.
 */
esp_err_t waveshare_sd_write_file(const char *path, const char *buf, size_t len);

/*
 * Benennt eine Datei um (atomar ersetzen). Ein vorhandenes Ziel wird zuvor
 * entfernt (FATFS-rename schlägt sonst fehl, wenn das Ziel existiert).
 *
 * @return ESP_OK bei Erfolg, ESP_FAIL sonst.
 */
esp_err_t waveshare_sd_rename(const char *from_path, const char *to_path);

/* true, wenn die SD-Karte erfolgreich gemountet wurde. */
bool waveshare_sd_port_is_mounted(void);

/* true, wenn die Datei unter path existiert (z. B. "/sdcard/dashboard.json"). */
bool waveshare_sd_port_file_exists(const char *path);

#ifdef __cplusplus
}
#endif

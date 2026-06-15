#pragma once

/*
 * Parser: JSON-String → dashboard_config_t.
 *
 * Reine Anwendungslogik, keine direkten ESP-IDF-/Hardware-Aufrufe.
 * Nativ testbar via pio test -e native:
 *   - esp_err.h wird dort über test/stubs/esp_err.h aufgelöst (Include-Pfad),
 *   - cJSON wird im native-Env als Library bereitgestellt.
 * Auf dem Gerät kommen die echten ESP-IDF-Header / das eingebaute cJSON zum Einsatz.
 */

#include "esp_err.h"
#include "config_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parst einen NUL-terminierten JSON-String in eine dashboard_config_t und löst
 * Widget→Signal-Bindungen auf (setzt widget_config.signal_idx).
 *
 * @param json_str  Eingabe-JSON (z. B. aus waveshare_sd_read_file())
 * @param cfg       Ziel-Struct (vom Aufrufer allokiert; wird vollständig gesetzt)
 * @param err_buf   Puffer für eine menschenlesbare Fehlermeldung (darf NULL sein)
 * @param err_len   Größe von err_buf in Bytes
 *
 * @return ESP_OK                bei Erfolg,
 *         ESP_ERR_INVALID_ARG   bei JSON-Syntaxfehler oder fehlendem Pflichtfeld,
 *         ESP_ERR_NOT_FOUND     wenn eine Widget-Signal-Referenz nicht auflösbar ist,
 *         ESP_ERR_INVALID_SIZE  wenn ein Limit (Signale/Seiten/Widgets) überschritten wird,
 *         ESP_ERR_INVALID_STATE bei ungültigem Wertebereich (z. B. min >= max).
 *
 * Bei Fehler enthält err_buf (falls != NULL) eine Beschreibung mit Kontext.
 * Unbekannte Widget-Typen sind KEIN Fehler – sie werden später in der
 * Widget-Registry übersprungen (FR-002); der Parser akzeptiert sie hier.
 */
esp_err_t config_loader_parse(const char *json_str,
                              dashboard_config_t *cfg,
                              char *err_buf, size_t err_len);

#ifdef __cplusplus
}
#endif

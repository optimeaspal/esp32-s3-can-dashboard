#pragma once

/*
 * HAL: HTTP-Server für den drahtlosen Upload der dashboard.json.
 *   GET  /              → /sdcard/www/index.html  (Fallback: eingebettetes HTML)
 *   GET  /<asset>       → /sdcard/www/<asset>      (app.js, style.css, …)
 *   GET  /api/config    → aktuelle dashboard.json  (Fallback: leeres Skelett)
 *   POST /api/config    → Raw-Body → temp → validieren → rename → Neustart
 */

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Startet den HTTP-Server auf Port `port`. */
esp_err_t web_server_start(uint16_t port);

#ifdef __cplusplus
}
#endif

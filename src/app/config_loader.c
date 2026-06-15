#include "config_loader.h"

#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* Logging nur auf dem Gerät; nativ (Tests) bewusst stumm, kein ESP-IDF-Include. */
#ifdef ESP_PLATFORM
#include "esp_log.h"
#define CFG_LOGW(...) ESP_LOGW("config_loader", __VA_ARGS__)
#else
#define CFG_LOGW(...) ((void)0)
#endif

/* ── Fehlermeldungs-Helfer ────────────────────────────────────────────────── */
static void set_err(char *buf, size_t len, const char *fmt, ...)
{
    if (!buf || len == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, len, fmt, ap);
    va_end(ap);
}

/* "#RRGGBB" oder "RRGGBB" → 0x00RRGGBB. Leerer/ungültiger String → 0. */
static uint32_t parse_color(const cJSON *item)
{
    if (!cJSON_IsString(item) || !item->valuestring) return 0;
    const char *s = item->valuestring;
    if (*s == '#') s++;
    return (uint32_t)strtoul(s, NULL, 16) & 0x00FFFFFFu;
}

/* CAN-ID aus String ("0x102"/"258") oder Zahl. Gibt false bei fehlendem Feld. */
static bool parse_can_id(const cJSON *item, uint32_t *out)
{
    if (cJSON_IsString(item) && item->valuestring) {
        *out = (uint32_t)strtoul(item->valuestring, NULL, 0);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        *out = (uint32_t)item->valuedouble;
        return true;
    }
    return false;
}

static double num_or(const cJSON *obj, const char *key, double def)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(it) ? it->valuedouble : def;
}

static bool bool_or(const cJSON *obj, const char *key, bool def)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it);
    return def;
}

static void str_copy(char *dst, size_t dstlen, const cJSON *it, const char *def)
{
    const char *src = (cJSON_IsString(it) && it->valuestring) ? it->valuestring : def;
    snprintf(dst, dstlen, "%s", src ? src : "");
}

/* ── Ein Signal parsen ────────────────────────────────────────────────────── */
static esp_err_t parse_signal(const cJSON *jsig, can_signal_t *sig,
                              char *err, size_t errlen)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(jsig, "name");
    const cJSON *cid  = cJSON_GetObjectItemCaseSensitive(jsig, "can_id");
    const cJSON *boff = cJSON_GetObjectItemCaseSensitive(jsig, "byte_offset");
    const cJSON *blen = cJSON_GetObjectItemCaseSensitive(jsig, "byte_length");
    const cJSON *endi = cJSON_GetObjectItemCaseSensitive(jsig, "endianness");

    if (!cJSON_IsString(name) || !name->valuestring) {
        set_err(err, errlen, "Signal: Pflichtfeld 'name' fehlt"); return ESP_ERR_INVALID_ARG;
    }
    memset(sig, 0, sizeof(*sig));
    snprintf(sig->name, sizeof(sig->name), "%s", name->valuestring);

    uint32_t can_id;
    if (!parse_can_id(cid, &can_id)) {
        set_err(err, errlen, "Signal '%s': Pflichtfeld 'can_id' fehlt/ungültig", sig->name);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsNumber(boff)) {
        set_err(err, errlen, "Signal '%s': Pflichtfeld 'byte_offset' fehlt", sig->name);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsNumber(blen)) {
        set_err(err, errlen, "Signal '%s': Pflichtfeld 'byte_length' fehlt", sig->name);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsString(endi) || !endi->valuestring) {
        set_err(err, errlen, "Signal '%s': Pflichtfeld 'endianness' fehlt", sig->name);
        return ESP_ERR_INVALID_ARG;
    }

    sig->can_id        = can_id;
    sig->extended_id   = (can_id > 0x7FF);
    sig->byte_offset   = (uint8_t)boff->valuedouble;
    sig->byte_length   = (uint8_t)blen->valuedouble;
    sig->little_endian = (strcmp(endi->valuestring, "big") != 0); /* default little */
    sig->is_signed     = bool_or(jsig, "signed", false);
    sig->is_float      = bool_or(jsig, "float", false);
    sig->is_simulated  = bool_or(jsig, "simulated", false);
    sig->scale         = (float)num_or(jsig, "scale", 1.0);
    sig->offset        = (float)num_or(jsig, "offset", 0.0);
    sig->min_value     = (float)num_or(jsig, "min", 0.0);
    sig->max_value     = (float)num_or(jsig, "max", 100.0);
    sig->timeout_ms    = (uint32_t)num_or(jsig, "stale_ms", 2000);
    str_copy(sig->unit, sizeof(sig->unit),
             cJSON_GetObjectItemCaseSensitive(jsig, "unit"), "");

    /* Wertebereich-Invariante */
    if (sig->min_value >= sig->max_value) {
        set_err(err, errlen, "Signal '%s': min (%.1f) >= max (%.1f)",
                sig->name, sig->min_value, sig->max_value);
        return ESP_ERR_INVALID_STATE;
    }
    if ((int)sig->byte_offset + (int)sig->byte_length > 8) {
        set_err(err, errlen, "Signal '%s': byte_offset+byte_length > 8", sig->name);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/* ── Ein Widget parsen ────────────────────────────────────────────────────── */
static esp_err_t parse_widget(const cJSON *jw, widget_config_t *w,
                              char *err, size_t errlen)
{
    memset(w, 0, sizeof(*w));
    w->signal_idx = -1;

    const cJSON *type   = cJSON_GetObjectItemCaseSensitive(jw, "type");
    const cJSON *x      = cJSON_GetObjectItemCaseSensitive(jw, "x");
    const cJSON *y      = cJSON_GetObjectItemCaseSensitive(jw, "y");
    const cJSON *width  = cJSON_GetObjectItemCaseSensitive(jw, "width");
    const cJSON *height = cJSON_GetObjectItemCaseSensitive(jw, "height");
    const cJSON *signal = cJSON_GetObjectItemCaseSensitive(jw, "signal");

    if (!cJSON_IsString(type) || !type->valuestring) {
        set_err(err, errlen, "Widget: Pflichtfeld 'type' fehlt"); return ESP_ERR_INVALID_ARG;
    }
    snprintf(w->type, sizeof(w->type), "%s", type->valuestring);

    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y) ||
        !cJSON_IsNumber(width) || !cJSON_IsNumber(height)) {
        set_err(err, errlen, "Widget '%s': x/y/width/height erforderlich", w->type);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsString(signal) || !signal->valuestring) {
        set_err(err, errlen, "Widget '%s': Pflichtfeld 'signal' fehlt", w->type);
        return ESP_ERR_INVALID_ARG;
    }

    w->x      = (int16_t)x->valuedouble;
    w->y      = (int16_t)y->valuedouble;
    w->width  = (uint16_t)width->valuedouble;
    w->height = (uint16_t)height->valuedouble;
    snprintf(w->signal_name, sizeof(w->signal_name), "%s", signal->valuestring);

    /* Style (alle optional) */
    str_copy(w->style.title, sizeof(w->style.title),
             cJSON_GetObjectItemCaseSensitive(jw, "title"), "");
    w->style.normal_color     = parse_color(cJSON_GetObjectItemCaseSensitive(jw, "normal_color"));
    w->style.warning_color    = parse_color(cJSON_GetObjectItemCaseSensitive(jw, "warning_color"));
    w->style.background_color  = parse_color(cJSON_GetObjectItemCaseSensitive(jw, "background_color"));
    w->style.warning_threshold = (float)num_or(jw, "warning_threshold", 0.0);
    w->style.has_warning = (w->style.warning_color != 0) || (w->style.warning_threshold != 0.0f);

    return ESP_OK;
}

/* Widget-Signal-Name → Index in cfg->signals[]; -1 wenn nicht gefunden. */
static int resolve_signal(const dashboard_config_t *cfg, const char *name)
{
    for (uint8_t i = 0; i < cfg->signal_count; i++) {
        if (strcmp(cfg->signals[i].name, name) == 0) return i;
    }
    return -1;
}

/* ── Öffentliche API ──────────────────────────────────────────────────────── */
esp_err_t config_loader_parse(const char *json_str,
                              dashboard_config_t *cfg,
                              char *err_buf, size_t err_len)
{
    if (!json_str || !cfg) {
        set_err(err_buf, err_len, "Interner Fehler: NULL-Argument");
        return ESP_ERR_INVALID_ARG;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->version_major = 1;

    esp_err_t rc = ESP_OK;
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        const char *e = cJSON_GetErrorPtr();
        set_err(err_buf, err_len, "JSON-Syntaxfehler nahe: %.32s", e ? e : "(unbekannt)");
        return ESP_ERR_INVALID_ARG;
    }

    /* version (optional, forward-compatible) */
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (cJSON_IsString(ver) && ver->valuestring) {
        unsigned maj = 1, min = 0;
        sscanf(ver->valuestring, "%u.%u", &maj, &min);
        cfg->version_major = (uint8_t)maj;
        cfg->version_minor = (uint8_t)min;
        if (maj > 1) CFG_LOGW("version %u.%u > 1.x – versuche fortzusetzen", maj, min);
    }

    /* signals (Pflicht) */
    const cJSON *signals = cJSON_GetObjectItemCaseSensitive(root, "signals");
    if (!cJSON_IsArray(signals)) {
        set_err(err_buf, err_len, "Pflichtfeld 'signals' fehlt oder ist kein Array");
        rc = ESP_ERR_INVALID_ARG; goto done;
    }

    const cJSON *jsig;
    cJSON_ArrayForEach(jsig, signals) {
        if (cfg->signal_count >= CFG_MAX_SIGNALS) {
            set_err(err_buf, err_len, "Zu viele Signale (max %d)", CFG_MAX_SIGNALS);
            rc = ESP_ERR_INVALID_SIZE; goto done;
        }
        can_signal_t *sig = &cfg->signals[cfg->signal_count];
        rc = parse_signal(jsig, sig, err_buf, err_len);
        if (rc != ESP_OK) goto done;

        /* Eindeutigkeit des Namens */
        for (uint8_t i = 0; i < cfg->signal_count; i++) {
            if (strcmp(cfg->signals[i].name, sig->name) == 0) {
                set_err(err_buf, err_len, "Signalname '%s' nicht eindeutig", sig->name);
                rc = ESP_ERR_INVALID_STATE; goto done;
            }
        }
        cfg->signal_count++;
    }

    /* tx_commands (FR-013): nur Existenz vermerken, Inhalt ignorieren */
    cfg->has_tx_commands = cJSON_HasObjectItem(root, "tx_commands");

    /* pages (Pflicht) */
    const cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (!cJSON_IsArray(pages)) {
        set_err(err_buf, err_len, "Pflichtfeld 'pages' fehlt oder ist kein Array");
        rc = ESP_ERR_INVALID_ARG; goto done;
    }

    const cJSON *jpage;
    cJSON_ArrayForEach(jpage, pages) {
        if (cfg->page_count >= CFG_MAX_PAGES) {
            set_err(err_buf, err_len, "Zu viele Seiten (max %d)", CFG_MAX_PAGES);
            rc = ESP_ERR_INVALID_SIZE; goto done;
        }
        page_config_t *page = &cfg->pages[cfg->page_count];
        memset(page, 0, sizeof(*page));
        str_copy(page->title, sizeof(page->title),
                 cJSON_GetObjectItemCaseSensitive(jpage, "title"), "");

        const cJSON *widgets = cJSON_GetObjectItemCaseSensitive(jpage, "widgets");
        if (!cJSON_IsArray(widgets)) {
            set_err(err_buf, err_len, "Seite %d: Pflichtfeld 'widgets' fehlt", cfg->page_count);
            rc = ESP_ERR_INVALID_ARG; goto done;
        }

        const cJSON *jw;
        cJSON_ArrayForEach(jw, widgets) {
            if (page->widget_count >= CFG_MAX_WIDGETS_PER_PAGE) {
                set_err(err_buf, err_len, "Seite %d: zu viele Widgets (max %d)",
                        cfg->page_count, CFG_MAX_WIDGETS_PER_PAGE);
                rc = ESP_ERR_INVALID_SIZE; goto done;
            }
            widget_config_t *w = &page->widgets[page->widget_count];
            rc = parse_widget(jw, w, err_buf, err_len);
            if (rc != ESP_OK) goto done;

            /* Signal-Binding auflösen */
            w->signal_idx = (int16_t)resolve_signal(cfg, w->signal_name);
            if (w->signal_idx < 0) {
                set_err(err_buf, err_len,
                        "Signal '%s' nicht definiert (Widget '%s' auf Seite %d)",
                        w->signal_name, w->type, cfg->page_count);
                rc = ESP_ERR_NOT_FOUND; goto done;
            }
            page->widget_count++;
        }
        cfg->page_count++;
    }

    rc = ESP_OK;

done:
    cJSON_Delete(root);
    return rc;
}

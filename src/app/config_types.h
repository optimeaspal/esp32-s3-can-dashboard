#pragma once

/*
 * Datenmodell für die JSON-basierte Dashboard-Konfiguration.
 *
 * Keine ESP-IDF-Abhängigkeiten → vollständig nativ kompilier- und testbar
 * (pio test -e native). Siehe specs/001-json-config-dashboard/data-model.md.
 */

#include <stdint.h>
#include <stdbool.h>
#include "can_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Limits (compile-time, defensiv gewählt, anpassbar) ───────────────────── */
#define CFG_MAX_SIGNALS          32
#define CFG_MAX_PAGES             8
#define CFG_MAX_WIDGETS_PER_PAGE 16
#define CFG_SIGNAL_NAME_LEN      CAN_SIGNAL_NAME_LEN  /* = 32 */
#define CFG_WIDGET_TITLE_LEN     32
#define CFG_WIDGET_TYPE_LEN      16   /* "gauge","chart","bar","led","label","arc" */

/* ── Erscheinungseigenschaften eines Widgets ──────────────────────────────── */
typedef struct
{
    uint32_t normal_color;       /* RGB 0xRRGGBB                                  */
    uint32_t warning_color;      /* RGB 0xRRGGBB (0 = nicht gesetzt → Default)    */
    uint32_t background_color;   /* RGB 0xRRGGBB (0 = transparent/Default)        */
    float    warning_threshold;  /* Wert ab dem warning_color greift (0 = keiner) */
    bool     has_warning;        /* true wenn warning_color/threshold gesetzt     */
    char     title[CFG_WIDGET_TITLE_LEN];
} widget_style_t;

/* ── Einzel-Widget ────────────────────────────────────────────────────────── */
typedef struct
{
    char     type[CFG_WIDGET_TYPE_LEN];        /* gauge|chart|bar|led|label|arc   */
    int16_t  x;
    int16_t  y;
    uint16_t width;
    uint16_t height;

    char     signal_name[CFG_SIGNAL_NAME_LEN]; /* Referenz auf signal.name        */
    int16_t  signal_idx;                       /* aufgelöst; -1 = unresolved      */

    widget_style_t style;
} widget_config_t;

/* ── Seite ────────────────────────────────────────────────────────────────── */
typedef struct
{
    char            title[CFG_WIDGET_TITLE_LEN];
    widget_config_t widgets[CFG_MAX_WIDGETS_PER_PAGE];
    uint8_t         widget_count;
} page_config_t;

/* ── Wurzel-Konfiguration ─────────────────────────────────────────────────── */
typedef struct
{
    uint8_t      version_major;
    uint8_t      version_minor;

    can_signal_t signals[CFG_MAX_SIGNALS];
    uint8_t      signal_count;

    page_config_t pages[CFG_MAX_PAGES];
    uint8_t       page_count;

    bool          has_tx_commands;  /* FR-013: tx_commands-Key vorhanden (v1: ignoriert) */
} dashboard_config_t;

#ifdef __cplusplus
}
#endif

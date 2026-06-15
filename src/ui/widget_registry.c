#include "widget_registry.h"
#include "widgets/widget_gauge.h"
#include "widgets/widget_chart.h"
#include "widgets/widget_bar.h"
#include "widgets/widget_led.h"
#include "widgets/widget_label.h"
#include "widgets/widget_arc.h"
#include <string.h>

/*
 * Statische Registrierungstabelle. Neue Widget-Typen (auch zukünftige
 * CAN-TX-Input-Widgets) werden hier durch eine Zeile ergänzt – kein Umbau
 * der Rendering-Schicht nötig (FR-014).
 */
static const widget_descriptor_t s_registry[] = {
    { "gauge", gauge_create, gauge_update, gauge_stale },
    { "chart", chart_create, chart_update, chart_stale },
    { "bar",   bar_create,   bar_update,   bar_stale   },
    { "led",   led_create,   led_update,   led_stale   },
    { "label", label_create, label_update, label_stale },
    { "arc",   arc_create,   arc_update,   arc_stale   },
};

static const size_t s_registry_count = sizeof(s_registry) / sizeof(s_registry[0]);

const widget_descriptor_t *widget_registry_find(const char *type_name)
{
    if (!type_name) return NULL;
    for (size_t i = 0; i < s_registry_count; i++) {
        if (strcmp(s_registry[i].type_name, type_name) == 0)
            return &s_registry[i];
    }
    return NULL;
}

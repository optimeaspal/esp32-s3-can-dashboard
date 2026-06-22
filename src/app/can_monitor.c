#include "can_monitor.h"
#include <string.h>

void can_monitor_reset(can_monitor_t *m)
{
    memset(m, 0, sizeof(*m));
}

static can_monitor_entry_t *find_slot(can_monitor_t *m, uint32_t id, bool extended)
{
    for (size_t i = 0; i < m->count; i++) {
        if (m->entries[i].id == id && m->entries[i].extended == extended)
            return &m->entries[i];
    }
    if (m->count >= CAN_MONITOR_MAX_IDS)
        return NULL;
    can_monitor_entry_t *e = &m->entries[m->count++];
    memset(e, 0, sizeof(*e));
    e->id       = id;
    e->extended = extended;
    return e;
}

void can_monitor_record(can_monitor_t *m, uint32_t id, bool extended,
                        const uint8_t *data, uint8_t dlc, int64_t timestamp_us)
{
    can_monitor_entry_t *e = find_slot(m, id, extended);
    if (!e) return;

    if (dlc > 8) dlc = 8;
    e->dlc = dlc;
    memset(e->data, 0, sizeof(e->data));
    if (data && dlc) memcpy(e->data, data, dlc);

    e->count++;
    e->window_count++;
    e->last_us = timestamp_us;
}

void can_monitor_update_fps(can_monitor_t *m, int64_t now_us)
{
    /* Erster Aufruf: Fensterbeginn übernehmen, noch keine Rate ausgeben. */
    if (!m->fps_initialized) {
        m->fps_window_start_us = now_us;
        m->fps_initialized     = true;
        return;
    }
    if (now_us - m->fps_window_start_us < CAN_MONITOR_FPS_WINDOW_US)
        return;
    for (size_t i = 0; i < m->count; i++) {
        m->entries[i].fps          = m->entries[i].window_count;
        m->entries[i].window_count = 0;
    }
    m->fps_window_start_us = now_us;
}

size_t can_monitor_snapshot(const can_monitor_t *m,
                            can_monitor_entry_t *out, size_t max)
{
    size_t n = (m->count < max) ? m->count : max;
    memcpy(out, m->entries, n * sizeof(can_monitor_entry_t));

    /* Insertion-Sort nach (extended, id) – n ist klein (<= 32). */
    for (size_t i = 1; i < n; i++) {
        can_monitor_entry_t key = out[i];
        size_t j = i;
        while (j > 0 &&
               ((out[j-1].extended > key.extended) ||
                (out[j-1].extended == key.extended && out[j-1].id > key.id))) {
            out[j] = out[j-1];
            j--;
        }
        out[j] = key;
    }
    return n;
}

uint32_t can_monitor_total_fps(const can_monitor_t *m)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < m->count; i++)
        sum += m->entries[i].fps;
    return sum;
}

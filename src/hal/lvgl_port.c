/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unverändert aus Waveshare-Demo übernommen (TAG-Definition in .c statt .h).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lvgl_port.h"

static const char *TAG = "lvgl_port";
static SemaphoreHandle_t lvgl_mux = NULL;
static TaskHandle_t lvgl_task_handle = NULL;

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0

static void *get_next_frame_buffer(esp_lcd_panel_handle_t panel_handle)
{
    static void *next_fb = NULL;
    static void *fb[2] = {NULL};
    if (next_fb == NULL) {
        ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb[0], &fb[1]));
        next_fb = fb[1];
    } else {
        next_fb = (next_fb == fb[0]) ? fb[1] : fb[0];
    }
    return next_fb;
}

IRAM_ATTR static void rotate_copy_pixel(
    const uint16_t *from, uint16_t *to,
    uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end,
    uint16_t w, uint16_t h, uint16_t rotation)
{
    int from_index = 0, to_index = 0, to_index_const = 0;
    switch (rotation) {
    case 90:
        to_index_const = (w - x_start - 1) * h;
        for (int fy = y_start; fy <= y_end; fy++) {
            from_index = fy * w + x_start;
            to_index = to_index_const + fy;
            for (int fx = x_start; fx <= x_end; fx++) {
                *(to + to_index) = *(from + from_index++);
                to_index -= h;
            }
        }
        break;
    case 180:
        to_index_const = h * w - x_start - 1;
        for (int fy = y_start; fy <= y_end; fy++) {
            from_index = fy * w + x_start;
            to_index = to_index_const - fy * w;
            for (int fx = x_start; fx <= x_end; fx++)
                *(to + to_index--) = *(from + from_index++);
        }
        break;
    case 270:
        to_index_const = (x_start + 1) * h - 1;
        for (int fy = y_start; fy <= y_end; fy++) {
            from_index = fy * w + x_start;
            to_index = to_index_const - fy;
            for (int fx = x_start; fx <= x_end; fx++) {
                *(to + to_index) = *(from + from_index++);
                to_index += h;
            }
        }
        break;
    default:
        break;
    }
}
#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */

#if LVGL_PORT_AVOID_TEAR_ENABLE
#if LVGL_PORT_DIRECT_MODE
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0

typedef struct {
    uint16_t inv_p;
    uint8_t  inv_area_joined[LV_INV_BUF_SIZE];
    lv_area_t inv_areas[LV_INV_BUF_SIZE];
} lv_port_dirty_area_t;

typedef enum { FLUSH_STATUS_PART, FLUSH_STATUS_FULL } lv_port_flush_status_t;
typedef enum { FLUSH_PROBE_PART_COPY, FLUSH_PROBE_SKIP_COPY, FLUSH_PROBE_FULL_COPY } lv_port_flush_probe_t;

static lv_port_dirty_area_t dirty_area;

static void flush_dirty_save(lv_port_dirty_area_t *da)
{
    lv_disp_t *disp = _lv_refr_get_disp_refreshing();
    da->inv_p = disp->inv_p;
    for (int i = 0; i < disp->inv_p; i++) {
        da->inv_area_joined[i] = disp->inv_area_joined[i];
        da->inv_areas[i] = disp->inv_areas[i];
    }
}

static lv_port_flush_probe_t flush_copy_probe(lv_disp_drv_t *drv)
{
    static lv_port_flush_status_t prev = FLUSH_STATUS_PART;
    lv_disp_t *disp = _lv_refr_get_disp_refreshing();
    uint32_t fv = 0, fh = 0;
    for (int i = 0; i < disp->inv_p; i++) {
        if (!disp->inv_area_joined[i]) {
            fv = disp->inv_areas[i].y2 + 1 - disp->inv_areas[i].y1;
            fh = disp->inv_areas[i].x2 + 1 - disp->inv_areas[i].x1;
            break;
        }
    }
    lv_port_flush_status_t cur = ((fv == (uint32_t)drv->ver_res) && (fh == (uint32_t)drv->hor_res))
                                     ? FLUSH_STATUS_FULL : FLUSH_STATUS_PART;
    lv_port_flush_probe_t result = (prev == FLUSH_STATUS_FULL)
                                       ? ((cur == FLUSH_STATUS_PART) ? FLUSH_PROBE_FULL_COPY : FLUSH_PROBE_SKIP_COPY)
                                       : FLUSH_PROBE_PART_COPY;
    prev = cur;
    return result;
}

static inline void *flush_get_next_buf(void *panel_handle)
{
    return get_next_frame_buffer(panel_handle);
}

static void flush_dirty_copy(void *dst, void *src, lv_port_dirty_area_t *da)
{
    for (int i = 0; i < da->inv_p; i++) {
        if (!da->inv_area_joined[i]) {
            rotate_copy_pixel(src, dst,
                              da->inv_areas[i].x1, da->inv_areas[i].y1,
                              da->inv_areas[i].x2, da->inv_areas[i].y2,
                              LV_HOR_RES, LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);
        }
    }
}

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    const int x1 = area->x1, x2 = area->x2, y1 = area->y1, y2 = area->y2;
    if (lv_disp_flush_is_last(drv)) {
        if (drv->full_refresh) {
            drv->full_refresh = 0;
            void *fb = flush_get_next_buf(panel);
            rotate_copy_pixel((uint16_t *)color_map, fb, x1, y1, x2, y2,
                              LV_HOR_RES, LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);
            esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, fb);
            ulTaskNotifyValueClear(NULL, ULONG_MAX);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            flush_dirty_copy(flush_get_next_buf(panel), color_map, &dirty_area);
            flush_get_next_buf(panel);
        } else {
            lv_port_flush_probe_t probe = flush_copy_probe(drv);
            if (probe == FLUSH_PROBE_FULL_COPY) {
                flush_dirty_save(&dirty_area);
                drv->full_refresh = 1;
                lv_disp_get_default()->rendering_in_progress = false;
                lv_disp_flush_ready(drv);
                lv_refr_now(_lv_refr_get_disp_refreshing());
            } else {
                void *fb = flush_get_next_buf(panel);
                flush_dirty_save(&dirty_area);
                flush_dirty_copy(fb, color_map, &dirty_area);
                esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, fb);
                ulTaskNotifyValueClear(NULL, ULONG_MAX);
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                if (probe == FLUSH_PROBE_PART_COPY) {
                    flush_dirty_save(&dirty_area);
                    flush_dirty_copy(flush_get_next_buf(panel), color_map, &dirty_area);
                    flush_get_next_buf(panel);
                }
            }
        }
    }
    lv_disp_flush_ready(drv);
}

#else /* ROTATION == 0, DIRECT_MODE */

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    if (lv_disp_flush_is_last(drv)) {
        esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
        ulTaskNotifyValueClear(NULL, ULONG_MAX);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    lv_disp_flush_ready(drv);
}
#endif /* ROTATION */

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_RGB_BUFFER_NUMS == 2

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    ulTaskNotifyValueClear(NULL, ULONG_MAX);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    lv_disp_flush_ready(drv);
}

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0
static void *lvgl_port_rgb_last_buf = NULL;
static void *lvgl_port_rgb_next_buf = NULL;
static void *lvgl_port_flush_next_buf = NULL;
#endif

void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0
    void *fb = get_next_frame_buffer(panel);
    rotate_copy_pixel((uint16_t *)color_map, fb, area->x1, area->y1, area->x2, area->y2,
                      LV_HOR_RES, LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, fb);
#else
    drv->draw_buf->buf1 = color_map;
    drv->draw_buf->buf2 = lvgl_port_flush_next_buf;
    lvgl_port_flush_next_buf = color_map;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lvgl_port_rgb_next_buf = color_map;
#endif
    lv_disp_flush_ready(drv);
}
#endif /* AVOID_TEAR mode */

#else /* AVOID_TEAR disabled */

void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lv_disp_flush_ready(drv);
}
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

static lv_disp_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    assert(panel_handle);
    static lv_disp_draw_buf_t disp_buf = {0};
    static lv_disp_drv_t disp_drv = {0};
    void *buf1 = NULL, *buf2 = NULL;
    int buffer_size = 0;

#if LVGL_PORT_AVOID_TEAR_ENABLE
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_V_RES;
#if (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0) && LVGL_PORT_FULL_REFRESH
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(
        panel_handle, 3, &lvgl_port_rgb_last_buf, &buf1, &buf2));
    lvgl_port_rgb_next_buf = lvgl_port_rgb_last_buf;
    lvgl_port_flush_next_buf = buf2;
#elif (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0)
    void *fbs[3];
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 3, &fbs[0], &fbs[1], &fbs[2]));
    buf1 = fbs[2];
#else
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));
#endif
#else
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT;
    buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LVGL_PORT_BUFFER_MALLOC_CAPS);
    assert(buf1);
    ESP_LOGI(TAG, "LVGL buffer: %d KB", buffer_size * sizeof(lv_color_t) / 1024);
#endif

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buffer_size);
    lv_disp_drv_init(&disp_drv);

#if defined(EXAMPLE_LVGL_PORT_ROTATION_90) || defined(EXAMPLE_LVGL_PORT_ROTATION_270)
    disp_drv.hor_res = LVGL_PORT_V_RES;
    disp_drv.ver_res = LVGL_PORT_H_RES;
#else
    disp_drv.hor_res = LVGL_PORT_H_RES;
    disp_drv.ver_res = LVGL_PORT_V_RES;
#endif
    disp_drv.flush_cb = flush_callback;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
#if LVGL_PORT_FULL_REFRESH
    disp_drv.full_refresh = 1;
#elif LVGL_PORT_DIRECT_MODE
    disp_drv.direct_mode = 1;
#endif
    return lv_disp_drv_register(&disp_drv);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)indev_drv->user_data;
    assert(tp);
    uint16_t tx, ty;
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, &tx, &ty, NULL, &cnt, 1);
    if (pressed && cnt > 0) {
        data->point.x = tx;
        data->point.y = ty;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp)
{
    assert(tp);
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    indev_drv.user_data = tp;
    return lv_indev_drv_register(&indev_drv);
}

static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static esp_err_t tick_init(void)
{
    const esp_timer_create_args_t args = {.callback = &tick_increment, .name = "lvgl_tick"};
    esp_timer_handle_t h = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&args, &h));
    return esp_timer_start_periodic(h, LVGL_PORT_TICK_PERIOD_MS * 1000);
}

static void lvgl_port_task(void *arg)
{
    uint32_t delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        if (delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle)
{
    lv_init();
    ESP_ERROR_CHECK(tick_init());

    lv_disp_t *disp = display_init(lcd_handle);
    assert(disp);

    if (tp_handle) {
        lv_indev_t *indev = indev_init(tp_handle);
        assert(indev);
#if defined(EXAMPLE_LVGL_PORT_ROTATION_90)
        esp_lcd_touch_set_swap_xy(tp_handle, true);
        esp_lcd_touch_set_mirror_y(tp_handle, true);
#elif defined(EXAMPLE_LVGL_PORT_ROTATION_180)
        esp_lcd_touch_set_mirror_x(tp_handle, true);
        esp_lcd_touch_set_mirror_y(tp_handle, true);
#elif defined(EXAMPLE_LVGL_PORT_ROTATION_270)
        esp_lcd_touch_set_swap_xy(tp_handle, true);
        esp_lcd_touch_set_mirror_x(tp_handle, true);
#endif
    }

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);

    BaseType_t core = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    BaseType_t ret = xTaskCreatePinnedToCore(
        lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE,
        NULL, LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "LVGL task creation failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool lvgl_port_lock(int timeout_ms)
{
    assert(lvgl_mux);
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(lvgl_mux);
    xSemaphoreGiveRecursive(lvgl_mux);
}

bool lvgl_port_notify_rgb_vsync(void)
{
    BaseType_t need_yield = pdFALSE;
#if LVGL_PORT_FULL_REFRESH && (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0)
    if (lvgl_port_rgb_next_buf != lvgl_port_rgb_last_buf) {
        lvgl_port_flush_next_buf = lvgl_port_rgb_last_buf;
        lvgl_port_rgb_last_buf = lvgl_port_rgb_next_buf;
    }
#elif LVGL_PORT_AVOID_TEAR_ENABLE
    xTaskNotifyFromISR(lvgl_task_handle, ULONG_MAX, eNoAction, &need_yield);
#endif
    return need_yield == pdTRUE;
}

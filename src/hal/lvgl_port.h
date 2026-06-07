/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal modifiziert: TAG aus Header entfernt, ansonsten 1:1 aus Waveshare-Demo.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LVGL_PORT_H_RES          (800)
#define LVGL_PORT_V_RES          (480)
#define LVGL_PORT_TICK_PERIOD_MS (CONFIG_EXAMPLE_LVGL_PORT_TICK)

#define LVGL_PORT_TASK_MAX_DELAY_MS  (CONFIG_EXAMPLE_LVGL_PORT_TASK_MAX_DELAY_MS)
#define LVGL_PORT_TASK_MIN_DELAY_MS  (CONFIG_EXAMPLE_LVGL_PORT_TASK_MIN_DELAY_MS)
#define LVGL_PORT_TASK_STACK_SIZE    (CONFIG_EXAMPLE_LVGL_PORT_TASK_STACK_SIZE_KB * 1024)
#define LVGL_PORT_TASK_PRIORITY      (CONFIG_EXAMPLE_LVGL_PORT_TASK_PRIORITY)
#define LVGL_PORT_TASK_CORE          (CONFIG_EXAMPLE_LVGL_PORT_TASK_CORE)

#if CONFIG_EXAMPLE_LVGL_PORT_BUF_PSRAM
#define LVGL_PORT_BUFFER_MALLOC_CAPS (MALLOC_CAP_SPIRAM)
#elif CONFIG_EXAMPLE_LVGL_PORT_BUF_INTERNAL
#define LVGL_PORT_BUFFER_MALLOC_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#endif
#define LVGL_PORT_BUFFER_HEIGHT (CONFIG_EXAMPLE_LVGL_PORT_BUF_HEIGHT)

#define LVGL_PORT_AVOID_TEAR_ENABLE (CONFIG_EXAMPLE_LVGL_PORT_AVOID_TEAR_ENABLE)
#if LVGL_PORT_AVOID_TEAR_ENABLE
#define LVGL_PORT_AVOID_TEAR_MODE      (CONFIG_EXAMPLE_LVGL_PORT_AVOID_TEAR_MODE)
#define EXAMPLE_LVGL_PORT_ROTATION_DEGREE (CONFIG_EXAMPLE_LVGL_PORT_ROTATION_DEGREE)

#if LVGL_PORT_AVOID_TEAR_MODE == 1
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS (2)
#define LVGL_PORT_FULL_REFRESH        (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 2
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS (3)
#define LVGL_PORT_FULL_REFRESH        (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 3
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS (2)
#define LVGL_PORT_DIRECT_MODE         (1)
#endif

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0
#define EXAMPLE_LVGL_PORT_ROTATION_0 (1)
#else
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 90
#define EXAMPLE_LVGL_PORT_ROTATION_90 (1)
#elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 180
#define EXAMPLE_LVGL_PORT_ROTATION_180 (1)
#elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 270
#define EXAMPLE_LVGL_PORT_ROTATION_270 (1)
#endif
#ifdef LVGL_PORT_LCD_RGB_BUFFER_NUMS
#undef LVGL_PORT_LCD_RGB_BUFFER_NUMS
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS (3)
#endif
#endif
#else
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS  (1)
#define LVGL_PORT_FULL_REFRESH         (0)
#define LVGL_PORT_DIRECT_MODE          (0)
#define EXAMPLE_LVGL_PORT_ROTATION_DEGREE (0)
#endif

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle);
bool      lvgl_port_lock(int timeout_ms);
void      lvgl_port_unlock(void);
bool      lvgl_port_notify_rgb_vsync(void);

#ifdef __cplusplus
}
#endif

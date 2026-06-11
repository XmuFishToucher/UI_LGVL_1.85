#include "lvgl_ui.h"
#include <stdbool.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "ST77916.h"
#include "CST816.h"
#include "ui_matrix.h"

static const char *TAG = "LVGL_UI";

static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;

extern void app_zero_calibrate(void);
extern void app_stim_set_enabled(bool enabled);

static void zero_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        app_zero_calibrate();
    }
}

static void create_zero_button(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *btn = lv_button_create(screen);
    lv_obj_set_size(btn, 70, 40);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 30, -50);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "ZERO");
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, zero_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

static void stim_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *sw = lv_event_get_target(e);
        app_stim_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
    }
}

static void create_stim_switch(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "STIM");
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, 45, -128);

    lv_obj_t *sw = lv_switch_create(screen);
    lv_obj_set_size(sw, 56, 30);
    lv_obj_align(sw, LV_ALIGN_BOTTOM_LEFT, 37, -95);
    lv_obj_add_event_cb(sw, stim_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

esp_err_t lvgl_ui_init(void)
{
    esp_err_t ret = ESP_OK;

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_GOTO_ON_ERROR(lvgl_port_init(&lvgl_cfg), err, TAG, "LVGL port init failed");

    // ================= LCD =================
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io_handle,
        .panel_handle = panel_handle,
        .buffer_size = 360 * 50,
        .double_buffer = true,
        .hres = 360,
        .vres = 360,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        }
    };

    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed to add display");
        return ESP_FAIL;
    }

    // ================= Touch =================
    if (tp != NULL) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lvgl_disp,
            .handle = tp,
        };
        lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);
    }

    // ================= ⭐关键修改：创建阵点UI =================
    lvgl_port_lock(0);

    ui_matrix_create();
    create_zero_button();
    create_stim_switch();

    lvgl_port_unlock();

    ESP_LOGI(TAG, "LVGL UI initialized (matrix mode)");
    return ESP_OK;

err:
    if (lvgl_disp != NULL) {
        lvgl_port_remove_disp(lvgl_disp);
        lvgl_disp = NULL;
    }
    return ret;
}

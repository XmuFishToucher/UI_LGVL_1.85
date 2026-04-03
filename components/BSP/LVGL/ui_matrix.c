#include "lvgl.h"
#include <stdio.h>

#define ROW 6
#define COL 6
#define POINT_NUM 36

static lv_obj_t *cells[POINT_NUM];

void ui_matrix_create(void)
{
    int screen_w = 320;
    int screen_h = 240;

    // 背景白色
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_100, 0);

    // ================== 手图 ==================
    LV_IMG_DECLARE(hand_map);

    lv_obj_t *img = lv_img_create(screen);
    lv_img_set_src(img, &hand_map);
    lv_obj_center(img);

    // ================== 圆点参数 ==================
    int dot_diameter = 8;
    int dot_spacing = 2;

    int start_x = 130;
    int start_y = 130;

    for (int i = 0; i < POINT_NUM; i++)
    {
        int r = i / COL;
        int c = i % COL;

        // ⭐ 保留你之前调好的映射
        r = ROW - 1 - r;
        c = COL - 1 - c;

        if (r < 3)
        {
            c = COL - 1 - c;
        }

        lv_obj_t *obj = lv_obj_create(screen);

        int pos_x = start_x + c * (dot_diameter + dot_spacing);
        int pos_y = start_y + r * (dot_diameter + dot_spacing);

        lv_obj_set_size(obj, dot_diameter, dot_diameter);
        lv_obj_set_pos(obj, pos_x, pos_y);

        // 圆形
        lv_obj_set_style_radius(obj, dot_diameter / 2, 0);

        // 初始黑色
        lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_100, 0);

        // 去边框
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        lv_obj_set_style_outline_width(obj, 0, 0);

        lv_obj_move_foreground(obj);

        cells[i] = obj;
    }

    printf("Matrix UI created: %d points\n", POINT_NUM);
}


// ================== 只负责显示（无颜色逻辑） ==================
static lv_color_t heatmap_color(uint8_t intensity)
{
    float t = intensity / 255.0f;

    uint8_t r, g, b;

    if (t < 0.25f)
    {
        // 蓝 → 青
        r = 0;
        g = (uint8_t)(t / 0.25f * 255);
        b = 255;
    }
    else if (t < 0.5f)
    {
        // 青 → 绿
        r = 0;
        g = 255;
        b = (uint8_t)((1.0f - (t - 0.25f) / 0.25f) * 255);
    }
    else if (t < 0.75f)
    {
        // 绿 → 黄
        r = (uint8_t)((t - 0.5f) / 0.25f * 255);
        g = 255;
        b = 0;
    }
    else
    {
        // 黄 → 红
        r = 255;
        g = (uint8_t)((1.0f - (t - 0.75f) / 0.25f) * 255);
        b = 0;
    }

    return lv_color_make(r, g, b);
}

void ui_matrix_update(uint16_t *cap)
{
    #define VALUE_MIN  5
    #define VALUE_MAX  115

    for (int i = 0; i < 36; i++)
    {
        uint16_t val = cap[i];

        uint8_t intensity;

        if (val < VALUE_MIN)
            intensity = 0;
        else if (val > VALUE_MAX)
            intensity = 255;
        else
            intensity = (val - VALUE_MIN) * 255 / (VALUE_MAX - VALUE_MIN);

        lv_color_t color = heatmap_color(intensity);

        lv_obj_set_style_bg_color(cells[i], color, 0);
    }

    lv_refr_now(NULL);
}
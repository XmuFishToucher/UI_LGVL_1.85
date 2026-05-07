#include "lvgl.h"
#include <stdio.h>
#include <string.h>

// ======================= 网格参数 =======================
#define TOTAL_POINTS 47
#define GRID_COLS    11
#define GRID_ROWS    9

// ======================= 通道 -> 网格位置映射表 =======================
// 每个通道在视觉网格中的 (col, row) 位置
// 拇指 col=0, 手掌 cols=1-9, 小指 col=10
// 手指区 rows=0-2 (屏上方), 手掌区 rows=3-8 (屏下方)

static const uint8_t ch_col[TOTAL_POINTS] = {
     0,                                            // ch 0:  拇指 (pin 2)
     1, 1, 1, 1, 1, 1,                            // ch 1-6:  手掌第1列 (pin 3-8, 上→下)
     2, 2, 2, 2, 2, 2,                            // ch 7-12: 手掌第2列 (pin 9-14, 上→下)
     3, 3, 3,                                      // ch 13-15: 食指 (pin 15-17, 下中上)
     4, 4, 4, 4, 4, 4,                            // ch 16-21: 手掌第3列 (pin 18-23, 上→下)
     5, 5, 5, 5, 5, 5,                            // ch 22-27: 手掌第4列 (pin 24-29, 下→上)
     6, 6, 6,                                      // ch 28-30: 中指 (pin 30-32, 下中上)
     7, 7, 7, 7, 7, 7,                            // ch 31-36: 手掌第5列 (pin 33-38, 下→上)
     8, 8, 8,                                      // ch 37-39: 无名指 (pin 39-41, 下中上)
     9, 9, 9, 9, 9, 9,                            // ch 40-45: 手掌第6列 (pin 42-47, 下→上)
    10                                             // ch 46: 小指 (pin 48)
};

static const uint8_t ch_row[TOTAL_POINTS] = {
     5,                                            // ch 0:  拇指 (手掌中段)
     3, 4, 5, 6, 7, 8,                            // ch 1-6:  手掌第1列 上→下
     3, 4, 5, 6, 7, 8,                            // ch 7-12: 手掌第2列 上→下
     2, 1, 0,                                      // ch 13-15: 食指 下→中→上 (pin 15/16/17, 视觉从下到上)
     3, 4, 5, 6, 7, 8,                            // ch 16-21: 手掌第3列 上→下
     8, 7, 6, 5, 4, 3,                            // ch 22-27: 手掌第4列 下→上 (pin 24-29 下→上)
     2, 1, 0,                                      // ch 28-30: 中指 下→中→上 (pin 30/31/32)
     8, 7, 6, 5, 4, 3,                            // ch 31-36: 手掌第5列 下→上 (pin 33-38 下→上)
     2, 1, 0,                                      // ch 37-39: 无名指 下→中→上 (pin 39/40/41)
     8, 7, 6, 5, 4, 3,                            // ch 40-45: 手掌第6列 下→上 (pin 42-47 下→上)
     0                                             // ch 46: 小指 (pin 48, 指尖)
};

static lv_obj_t *cells[TOTAL_POINTS];

// ======================= 创建阵点UI =======================
void ui_matrix_create(void)
{
    // 背景白色
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_100, 0);

    // ================== 手图背景 ==================
    LV_IMG_DECLARE(hand_map);

    lv_obj_t *img = lv_img_create(screen);
    lv_img_set_src(img, &hand_map);
    lv_obj_center(img);

    // ================== 圆点参数 ==================
    int dot_diameter = 8;
    int dot_spacing = 2;
    int cell_size = dot_diameter + dot_spacing;   // 10px per cell

    // 网格总尺寸: 11列×9行 = 110×90 px, 在360×360屏上居中
    int grid_w = GRID_COLS * cell_size;            // 110
    int grid_h = GRID_ROWS * cell_size;            // 90
    int start_x = (360 - grid_w) / 2;              // 125
    int start_y = (360 - grid_h) / 2;              // 135

    // ================== 为每个通道创建圆点 ==================
    for (int ch = 0; ch < TOTAL_POINTS; ch++)
    {
        int col = ch_col[ch];
        int row = ch_row[ch];

        lv_obj_t *obj = lv_obj_create(screen);

        int pos_x = start_x + col * cell_size;
        int pos_y = start_y + row * cell_size;

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

        cells[ch] = obj;
    }

    printf("Matrix UI created: %d points, %dx%d grid\n", TOTAL_POINTS, GRID_COLS, GRID_ROWS);
}


// ================== 热力图颜色映射 ==================
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

#define VALUE_MAX 80

// ================== 更新阵点颜色 ==================
void ui_matrix_update(uint16_t *cap)
{
    for (int i = 0; i < TOTAL_POINTS; i++)
    {
        uint16_t val = cap[i];
        uint8_t intensity;

        if (val > VALUE_MAX)
            intensity = 255;
        else
            intensity = val * 255 / VALUE_MAX;

        lv_color_t color = heatmap_color(intensity);
        lv_obj_set_style_bg_color(cells[i], color, 0);
    }

    lv_refr_now(NULL);
}

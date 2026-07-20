#include "lvgl.h"
#include <stdio.h>
#include <string.h>

// ======================= 网格参数 =======================
#define TOTAL_POINTS 47

// ======================= 通道 -> 网格位置映射表 =======================
// 使用浮点网格坐标 (gx, gy), 参照 LCD-1.69 方式
// 手掌: gx=0..5, gy=0..5 形成 6×6 矩形, 列间距=行间距=1格
// 手指: 独立浮点坐标, 位于手掌上方
// ch 0→小指, ch 46→拇指 (通道号已反转)

typedef struct {
    uint8_t ch;
    float gx;
    float gy;
} point_def_t;

static const point_def_t points[TOTAL_POINTS] = {

    // ===== 小指 (ch 0, pin 48) =====
    { 0,  6.8f, -4.0f},

    // ===== 手掌第6列 (ch 1-6, pins 47-42, gx=5, gy=0..5 上→下) =====
    { 1,  5.0f, 0},  { 2,  5.0f, 1},  { 3,  5.0f, 2},
    { 4,  5.0f, 3},  { 5,  5.0f, 4},  { 6,  5.0f, 5},

    // ===== 无名指 (ch 7-9, pins 41-39, 指尖→指根) =====
    { 7,  4.3f, -6.4f},  { 8,  4.0f, -4.3f},  { 9,  3.7f, -2.5f},

    // ===== 手掌第5列 (ch 10-15, pins 38-33, gx=4, gy=0..5 上→下) =====
    {10,  4.0f, 0},  {11,  4.0f, 1},  {12,  4.0f, 2},
    {13,  4.0f, 3},  {14,  4.0f, 4},  {15,  4.0f, 5},

    // ===== 中指 (ch 16-18, pins 32-30, 指尖→指根) =====
    {16,  2.2f, -7.5f},  {17,  2.0f, -5.0f},  {18,  2.0f, -3.1f},

    // ===== 手掌第4列 (ch 19-24, pins 29-24, gx=3, gy=0..5 上→下) =====
    {19,  3.0f, 0},  {20,  3.0f, 1},  {21,  3.0f, 2},
    {22,  3.0f, 3},  {23,  3.0f, 4},  {24,  3.0f, 5},

    // ===== 手掌第3列 (ch 25-30, pins 23-18, gx=2, gy=5..0 上→下) =====
    {25,  2.0f, 5},  {26,  2.0f, 4},  {27,  2.0f, 3},
    {28,  2.0f, 2},  {29,  2.0f, 1},  {30,  2.0f, 0},

    // ===== 食指 (ch 31-33, pins 17-15, 指尖→指根) =====
    {31,  -1.0f, -6.0f},  {32,  -0.7f, -4.3f},  {33,  -0.5f, -2.5f},

    // ===== 手掌第2列 (ch 34-39, pins 14-9, gx=1, gy=5..0 上→下) =====
    {34,  1.0f, 5},  {35,  1.0f, 4},  {36,  1.0f, 3},
    {37,  1.0f, 2},  {38,  1.0f, 1},  {39,  1.0f, 0},

    // ===== 手掌第1列 (ch 40-45, pins 8-3, gx=0, gy=5..0 上→下) =====
    {40,  0.0f, 5},  {41,  0.0f, 4},  {42,  0.0f, 3},
    {43,  0.0f, 2},  {44,  0.0f, 1},  {45,  0.0f, 0},

    // ===== 拇指 (ch 46, pin 2) =====
    {46, -4.5f, 0.0f},
};

static lv_obj_t *cells[TOTAL_POINTS];

#define PALM_Y_OFFSET (-0.5f)

static int is_palm_point(uint8_t ch)
{
    return (ch >= 1 && ch <= 6) ||
           (ch >= 10 && ch <= 15) ||
           (ch >= 19 && ch <= 30) ||
           (ch >= 34 && ch <= 45);
}

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
    int dot_diameter = 12;
    int dot_spacing = 3;
    int cell_size = dot_diameter + dot_spacing;   // 20px per grid unit

    // 坐标范围: gx=-2.5..5.5, gy=-5.0..6.0
    float min_gx = -2.5f, max_gx = 5.5f;
    float min_gy = -5.0f, max_gy = 6.0f;
    int grid_w = (int)((max_gx - min_gx) * cell_size) - dot_spacing;
    int grid_h = (int)((max_gy - min_gy) * cell_size) - dot_spacing;
    int start_x = (360 - grid_w) / 2;
    int start_y = (360 - grid_h) / 2;

    // ================== 为每个通道创建圆点 ==================
    for (int i = 0; i < TOTAL_POINTS; i++)
    {
        lv_obj_t *obj = lv_obj_create(screen);

        float gy = points[i].gy + (is_palm_point(points[i].ch) ? PALM_Y_OFFSET : 0.0f);
        int pos_x = start_x + (int)((points[i].gx - min_gx) * cell_size);
        int pos_y = start_y + (int)((gy - min_gy) * cell_size);

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
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);

        lv_obj_move_foreground(obj);

        cells[i] = obj;
    }

    printf("Matrix UI created: %d points, grid %dx%d at (%d,%d)\r\n",
           TOTAL_POINTS, grid_w, grid_h, start_x, start_y);
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
        uint16_t val = cap[points[i].ch];
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

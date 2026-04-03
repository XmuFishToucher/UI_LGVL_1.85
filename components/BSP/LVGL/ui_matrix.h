#ifndef UI_MATRIX_H
#define UI_MATRIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建6x6阵点UI（带手图背景）
 */
void ui_matrix_create(void);

/**
 * @brief 更新36个阵点数据（通过颜色显示）
 * 
 * @param cap 36个点的数据数组
 */
void ui_matrix_update(uint16_t *cap);

#ifdef __cplusplus
}
#endif

#endif // UI_MATRIX_H
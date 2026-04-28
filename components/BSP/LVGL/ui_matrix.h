#ifndef UI_MATRIX_H
#define UI_MATRIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建阵点UI（带手图背景），47点按物理布局映射
 */
void ui_matrix_create(void);

/**
 * @brief 更新47个阵点数据（通过颜色显示）
 *
 * @param cap 47个点的数据数组
 */
void ui_matrix_update(uint16_t *cap);

#ifdef __cplusplus
}
#endif

#endif // UI_MATRIX_H
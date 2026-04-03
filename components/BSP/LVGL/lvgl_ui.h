#ifndef LVGL_UI_H
#define LVGL_UI_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LVGL UI and display "Hello LVGL" in the center of screen
 *
 * This function initializes LVGL port using BSP components and creates
 * a label with "Hello LVGL" text centered on the screen.
 *
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t lvgl_ui_init(void);

#ifdef __cplusplus
}
#endif

#endif // LVGL_UI_H
#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化蜂鸣器控制引脚
 *
 * @param gpio         蜂鸣器控制引脚（如 GPIO_NUM_4）
 * @param active_level 有效触发电平：1 = 高电平响，0 = 低电平响
 * @return esp_err_t   ESP_OK 成功；引脚非法时返回 ESP_ERR_INVALID_ARG
 */
esp_err_t buzzer_init(gpio_num_t gpio, int active_level);

/** @brief 设置蜂鸣器状态，true = 响，false = 停（自动换算有效电平） */
void buzzer_set(bool on);

/** @brief 鸣响 */
void buzzer_on(void);

/** @brief 停响 */
void buzzer_off(void);

#ifdef __cplusplus
}
#endif

/* 仅供 PC 端静态编译检查用的 ESP-IDF 头文件桩，不会被编译进固件 */
#pragma once

typedef int esp_err_t;
#define ESP_OK                 0
#define ESP_FAIL              -1
#define ESP_ERR_NO_MEM        -2
#define ESP_ERR_INVALID_ARG   -3
#define ESP_ERR_INVALID_STATE -4
#define ESP_ERR_INVALID_SIZE  -5
#define ESP_ERR_NOT_FOUND     -6
#define ESP_ERR_TIMEOUT       -7

typedef enum {
    GPIO_NUM_0 = 0,
} gpio_num_t;

static inline const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "stub";
}

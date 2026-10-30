#pragma once

/**
 * SSD1306 128x64 OLED 驱动（I2C 传输层）
 * ---------------------------------------------------------------
 * 只负责总线通信与刷屏，所有绘制由 gfx.c 完成（先在 RAM 里画完再整屏推送，
 * 所以画面不会撕裂/闪烁）。
 *
 * 兼容:
 *   - ESP-IDF v4.4 ~ v5.x : legacy i2c driver (driver/i2c.h)
 *   - ESP-IDF v6.x        : 新 i2c master driver (driver/i2c_master.h)
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH  GFX_WIDTH
#define SSD1306_HEIGHT GFX_HEIGHT

/**
 * @brief 初始化 I2C 与 SSD1306
 *
 * @param sda_gpio  SDA 引脚
 * @param scl_gpio  SCL 引脚
 * @param i2c_addr  7 位地址，常见 0x3C（模块背面 SA0 悬空/接 GND）或 0x3D（SA0 接 VCC）
 */
esp_err_t ssd1306_init(int sda_gpio, int scl_gpio, uint8_t i2c_addr);

/** @brief 把 gfx 帧缓冲推到屏幕（1024 字节，约 23ms @400kHz） */
esp_err_t ssd1306_flush(void);

/** @brief 对比度 0~255，默认 0xCF */
esp_err_t ssd1306_set_contrast(uint8_t contrast);

/** @brief 屏幕开关；关闭后电流 <10uA，适合长时间待机 */
esp_err_t ssd1306_power(bool on);

#ifdef __cplusplus
}
#endif

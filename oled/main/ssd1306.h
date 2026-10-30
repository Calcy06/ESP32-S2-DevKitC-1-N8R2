#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SSD1306 设备对象。
 *
 * 显存（framebuffer）放在 ESP32 内部 RAM 里：
 * 128x64 单色屏 = 128 * 64 / 8 = 1024 字节，非常小。
 * 做法：先在 MCU 内存里把画面画好，再一次性推到屏幕的 GDDRAM。
 */
typedef struct {
    i2c_master_bus_handle_t bus;      /* I2C 总线句柄 */
    i2c_master_dev_handle_t i2c_dev;  /* SSD1306 的 I2C 设备句柄 */
    uint8_t addr;                     /* 7 位 I2C 地址（通常 0x3C） */
    uint8_t width;                    /* 屏幕宽度像素（128） */
    uint8_t height;                   /* 屏幕高度像素（64 或 32） */
    uint8_t *fb;                      /* 显存指针，width * height / 8 字节 */
} ssd1306_t;

/**
 * @brief 初始化 I2C 总线 + SSD1306
 *
 * @param dev        设备对象（调用方提供）
 * @param port       I2C 端口号，ESP32-S2 有 I2C_NUM_0 / I2C_NUM_1
 * @param sda_gpio   SDA 引脚
 * @param scl_gpio   SCL 引脚
 * @param i2c_addr   7 位地址，常见 0x3C（少数模块是 0x3D）
 * @param width      屏幕宽度，通常 128
 * @param height     屏幕高度，64 或 32
 * @return esp_err_t
 */
esp_err_t ssd1306_init(ssd1306_t *dev, i2c_port_t port, gpio_num_t sda_gpio,
                       gpio_num_t scl_gpio, uint8_t i2c_addr,
                       uint8_t width, uint8_t height);

/** 把整块显存推送到屏幕（画完内容后必须调用，否则屏幕不变） */
esp_err_t ssd1306_refresh(ssd1306_t *dev);

/** 清空显存（注意：清的是内存，不会立刻改变屏幕，需再调 refresh） */
void ssd1306_clear(ssd1306_t *dev);

/** 画一个像素点，on=true 点亮 */
void ssd1306_draw_pixel(ssd1306_t *dev, uint8_t x, uint8_t y, bool on);

/** 画一个字符，x/y 是左上角像素坐标 */
void ssd1306_draw_char(ssd1306_t *dev, uint8_t x, uint8_t y, char c);

/** 画一行字符串，x/y 是左上角像素坐标 */
void ssd1306_draw_string(ssd1306_t *dev, uint8_t x, uint8_t y, const char *str);

/** 释放显存（一般不需要调用） */
void ssd1306_deinit(ssd1306_t *dev);

#ifdef __cplusplus
}
#endif

/**
 * ESP32-S2 + SSD1306 OLED（I2C）示例 —— 显示一行文本
 *
 * 硬件：ESP32-S2-DevKitC-1-N8R2（8MB Flash + 2MB PSRAM）
 *       0.96 寸 SSD1306 OLED（128x64，I2C 接口，地址通常 0x3C）
 *
 * 【本版本特点：零外部组件】
 *   - 不含 Kconfig.projbuild、不含 sdkconfig.defaults。
 *   - I2C 引脚 / 屏幕尺寸 / 显示内容都在下面这几个 #define 里改。
 *   - SSD1306 驱动（ssd1306.c）和 5x7 字体（font5x7.h）都是自己写的，
 *     不需要 add-dependency 下载任何组件。见 README 第 3 节。
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "font5x7.h"
#include "ssd1306.h"

static const char *TAG = "oled_demo";

/* ========================================================================== */
/* 参数区：想改引脚 / 尺寸 / 显示内容，改这里然后重新 idf.py build            */
/* ========================================================================== */
#define OLED_SDA_GPIO   GPIO_NUM_8   /* SDA，DevKitC-1 的 J3 第 12 脚          */
#define OLED_SCL_GPIO   GPIO_NUM_9   /* SCL，DevKitC-1 的 J3 第 15 脚          */
#define OLED_I2C_PORT   I2C_NUM_0    /* ESP32-S2 有 I2C_NUM_0 / I2C_NUM_1      */
#define OLED_I2C_ADDR   0x3C         /* 7 位地址。若不亮就试 0x3D              */
#define OLED_WIDTH      128          /* 0.96 寸屏通常是 128                    */
#define OLED_HEIGHT     64           /* 128x64 填 64；128x32 的屏填 32         */

#define OLED_TEXT       "Hello ESP32-S2!"   /* 要显示的那一行文本 */

void app_main(void)
{
    ESP_LOGI(TAG, "SSD1306 OLED demo start");

    ssd1306_t oled = {0};

    /* 1. 初始化 I2C + SSD1306（内部会发初始化命令序列并开屏） */
    esp_err_t ret = ssd1306_init(&oled, OLED_I2C_PORT, OLED_SDA_GPIO, OLED_SCL_GPIO,
                                 OLED_I2C_ADDR, OLED_WIDTH, OLED_HEIGHT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306 init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "检查接线、屏幕地址(0x3C/0x3D)、以及 menuconfig 里 I2C 是否可用");
        return;
    }

    /* 2. 清显存（此时屏幕还没变化，清的是 MCU 内存里的画面） */
    ssd1306_clear(&oled);

    /* 3. 在显存里画一行文字 —— 水平垂直都居中 */
    uint8_t text_w = (uint8_t)(strlen(OLED_TEXT) * (FONT5X7_WIDTH + 1));
    uint8_t x = (OLED_WIDTH  > text_w)            ? (OLED_WIDTH  - text_w)             / 2 : 0;
    uint8_t y = (OLED_HEIGHT > FONT5X7_HEIGHT)    ? (OLED_HEIGHT - FONT5X7_HEIGHT)     / 2 : 0;
    ssd1306_draw_string(&oled, x, y, OLED_TEXT);

    /* 想多显示几行就再调几次，y 每次加 8~10 即可，例如：
     *   ssd1306_draw_string(&oled, 0, 16, "line 2");
     *   ssd1306_draw_string(&oled, 0, 26, "line 3");
     */

    /* 4. ★ 把显存一次性推到屏幕。漏掉这句屏幕不会有任何变化 */
    ESP_ERROR_CHECK(ssd1306_refresh(&oled));

    ESP_LOGI(TAG, "display updated: \"%s\" at (%d, %d)", OLED_TEXT, x, y);

    /* 想做动态刷新：清显存 → 重画 → refresh，放进循环里即可，例如
     *   while (1) {
     *       ssd1306_clear(&oled);
     *       ssd1306_draw_string(&oled, 0, 0, buf);
     *       ssd1306_refresh(&oled);
     *       vTaskDelay(pdMS_TO_TICKS(1000));
     *   }
     */
}

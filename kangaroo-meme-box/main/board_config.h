#pragma once

/* 引脚与参数通过 menuconfig 配置，这里给一份默认值兜底 */

/* I2C (SSD1306 OLED) */
#ifndef CONFIG_OLED_SDA_GPIO
#define CONFIG_OLED_SDA_GPIO 8
#endif
#ifndef CONFIG_OLED_SCL_GPIO
#define CONFIG_OLED_SCL_GPIO 9
#endif
#ifndef CONFIG_OLED_I2C_ADDR
#define CONFIG_OLED_I2C_ADDR 0x3C
#endif

/* 有源蜂鸣器（高电平触发），硬件实际接在 GPIO4 */
#ifndef CONFIG_BUZZER_GPIO
#define CONFIG_BUZZER_GPIO 4
#endif

/* 触发按键（板载 BOOT 键 = GPIO0） */
#ifndef CONFIG_BUTTON_GPIO
#define CONFIG_BUTTON_GPIO 0
#endif

/* 帧率：25fps 足够顺滑，且给 I2C 留出余量 */
#ifndef CONFIG_FRAME_INTERVAL_MS
#define CONFIG_FRAME_INTERVAL_MS 40
#endif

/* 待机自动播放间隔(秒)，0 = 不自动播放 */
#ifndef CONFIG_IDLE_AUTOSHOW_SEC
#define CONFIG_IDLE_AUTOSHOW_SEC 10
#endif

/*
 * 软件在环仿真器 —— 用桩实现 ESP-IDF 运行时，把固件逻辑在 PC 上完整跑一遍
 *
 * 目的：不接板子、不装工具链，也能验证
 *   1. 状态机（开机 → 待机 → 台词 → 待机）是否按预期流转
 *   2. 字幕滚动、动画参数是否正常
 *   3. 按键短按/长按是否被正确识别
 *   4. 蜂鸣器节奏的开关时序
 *
 * 编译:
 *   gcc -O2 -I../main -Iesp_stubs sim.c ../main/*.c -lm -o sim
 * 运行:
 *   ./sim            # 生成 frames.bin（每帧 1024 字节，按 40ms 一帧）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "gfx.h"

void app_main(void);

#define FRAME_MS   40
#define MAX_FRAMES 500          /* 20 秒虚拟时间 */

static jmp_buf g_jb;
static int64_t g_now_us;
static int g_frame;
static FILE *g_out;
static int g_buzzer_level;
static long g_buzz_on_ms;
static int64_t g_buzz_since;

/* ---------------------------------------------------------------- 时钟 */

int64_t esp_timer_get_time(void)
{
    return g_now_us;
}

uint32_t esp_random(void)
{
    static uint32_t s = 20261030u;
    s = s * 1103515245u + 12345u;
    return s >> 8;
}

void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
    /* 每个 tick 结束时帧缓冲已经画好，正好抓一帧 */
    fwrite(gfx_fb(), 1, GFX_FB_SIZE, g_out);
    g_frame++;
    g_now_us += (int64_t)FRAME_MS * 1000;
    if (g_frame >= MAX_FRAMES) {
        longjmp(g_jb, 1);
    }
}

/* ---------------------------------------------------------------- GPIO */

esp_err_t gpio_config(const gpio_config_t *cfg)
{
    (void)cfg;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level)
{
    if (gpio == 10) {                    /* CONFIG_BUZZER_GPIO 默认值 */
        if (level && !g_buzzer_level) {
            g_buzz_since = g_now_us;
        } else if (!level && g_buzzer_level) {
            g_buzz_on_ms += (long)((g_now_us - g_buzz_since) / 1000);
        }
        g_buzzer_level = level;
    }
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio)
{
    if (gpio == 0) {                     /* BOOT 键：低 = 按下 */
        /* 60~62 帧：短按（120ms），应触发"下一句"
         * 200~235 帧：长按（1400ms），应触发"静音开关" */
        if ((g_frame >= 60 && g_frame < 63) || (g_frame >= 200 && g_frame < 235)) {
            return 0;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------- I2C */

esp_err_t i2c_param_config(i2c_port_t port, const i2c_config_t *conf)
{
    (void)port; (void)conf;
    return ESP_OK;
}

esp_err_t i2c_driver_install(i2c_port_t port, i2c_mode_t mode, size_t rx, size_t tx, int flags)
{
    (void)port; (void)mode; (void)rx; (void)tx; (void)flags;
    return ESP_OK;
}

i2c_cmd_handle_t i2c_cmd_link_create(void) { return (void *)1; }
void i2c_cmd_link_delete(i2c_cmd_handle_t cmd) { (void)cmd; }
esp_err_t i2c_master_start(i2c_cmd_handle_t cmd) { (void)cmd; return ESP_OK; }
esp_err_t i2c_master_stop(i2c_cmd_handle_t cmd) { (void)cmd; return ESP_OK; }

esp_err_t i2c_master_write_byte(i2c_cmd_handle_t cmd, uint8_t data, bool ack)
{
    (void)cmd; (void)data; (void)ack;
    return ESP_OK;
}

esp_err_t i2c_master_write(i2c_cmd_handle_t cmd, uint8_t *data, size_t len, bool ack)
{
    (void)cmd; (void)data; (void)len; (void)ack;
    return ESP_OK;
}

esp_err_t i2c_master_cmd_begin(i2c_port_t port, i2c_cmd_handle_t cmd, uint32_t ticks)
{
    (void)port; (void)cmd; (void)ticks;
    return ESP_OK;
}

/* ---------------------------------------------------------------- main */

int main(void)
{
    g_out = fopen("frames.bin", "wb");
    if (!g_out) {
        perror("frames.bin");
        return 1;
    }

    if (setjmp(g_jb) == 0) {
        app_main();
    }

    fclose(g_out);
    printf("\n==== 仿真结束 ====\n");
    printf("总帧数 : %d (%.1f 秒 / %d fps)\n", g_frame,
           g_frame * FRAME_MS / 1000.0, 1000 / FRAME_MS);
    printf("蜂鸣器累计响 %ld ms\n", g_buzz_on_ms);
    printf("输出   : frames.bin (%d 字节)\n", g_frame * GFX_FB_SIZE);
    return 0;
}

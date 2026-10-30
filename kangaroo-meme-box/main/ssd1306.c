#include "ssd1306.h"

#include <string.h>
#include "esp_log.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if ESP_IDF_VERSION_MAJOR >= 6
#include "driver/i2c_master.h"
#else
#include "driver/i2c.h"
#endif

static const char *TAG = "ssd1306";

#define SSD1306_CTRL_CMD  0x00
#define SSD1306_CTRL_DATA 0x40

static uint8_t s_addr;
static bool s_ready;

/* 刷屏缓冲：1 控制字节 + 1024 数据。静态分配，避免占任务栈 */
static uint8_t s_txbuf[1 + GFX_FB_SIZE];

#if ESP_IDF_VERSION_MAJOR >= 6
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
#endif

/* ================================================================== I2C 层 */

static esp_err_t i2c_write_raw(const uint8_t *data, size_t len)
{
#if ESP_IDF_VERSION_MAJOR >= 6
    return i2c_master_transmit(s_dev, data, len, 100);
#else
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
#endif
}

static esp_err_t ssd1306_cmd(uint8_t c)
{
    uint8_t buf[2] = {SSD1306_CTRL_CMD, c};
    return i2c_write_raw(buf, sizeof(buf));
}

static esp_err_t ssd1306_cmd2(uint8_t c, uint8_t arg)
{
    uint8_t buf[3] = {SSD1306_CTRL_CMD, c, arg};
    return i2c_write_raw(buf, sizeof(buf));
}

/* ================================================================== 初始化 */

esp_err_t ssd1306_init(int sda_gpio, int scl_gpio, uint8_t i2c_addr)
{
    s_addr = i2c_addr;

#if ESP_IDF_VERSION_MAJOR >= 6
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        return err;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device: %s", esp_err_to_name(err));
        return err;
    }
#else
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config: %s", esp_err_to_name(err));
        return err;
    }
    err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install: %s", esp_err_to_name(err));
        return err;
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(10));

    static const uint8_t seq[][2] = {
        {0xAE, 0x00},  /* 关显示 */
        {0xD5, 0x80},  /* 时钟分频 */
        {0xA8, 0x3F},  /* 复用率 1/64 */
        {0xD3, 0x00},  /* 显示偏移 0 */
        {0x40, 0x00},  /* 起始行 0 */
        {0x8D, 0x14},  /* 电荷泵开 */
        {0x20, 0x00},  /* 水平寻址 */
        {0xA1, 0x00},  /* 段重映射 */
        {0xC8, 0x00},  /* COM 反向 */
        {0xDA, 0x12},  /* COM 引脚 */
        {0x81, 0xCF},  /* 对比度 */
        {0xD9, 0xF1},  /* 预充电 */
        {0xDB, 0x40},  /* VCOMH */
        {0xA4, 0x00},  /* 跟随 RAM */
        {0xA6, 0x00},  /* 正常显示 */
        {0x2E, 0x00},  /* 关滚动 */
        {0xAF, 0x00},  /* 开显示 */
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        err = ssd1306_cmd2(seq[i][0], seq[i][1]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "init cmd 0x%02X failed: %s", seq[i][0], esp_err_to_name(err));
            return err;
        }
    }

    /*
     * 注：如果买到的是 SH1106（1.3 寸常见），它的 RAM 是 132 列、可见窗口
     * 从第 2 列开始，需要把上面的列地址窗口从 0~127 改成 2~129，否则画面
     * 会整体左移 2 像素、右边缺一条。改 ssd1306_flush() 里的 win[] 即可。
     */
    gfx_clear();
    s_ready = true;
    err = ssd1306_flush();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "first flush failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SSD1306 ready @0x%02X (SDA=%d SCL=%d)", i2c_addr, sda_gpio, scl_gpio);
    return ESP_OK;
}

esp_err_t ssd1306_flush(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 设置列窗口 0~127、页窗口 0~7，之后连续写 1024 字节 */
    static const uint8_t win[6] = {0x21, 0, 127, 0x22, 0, 7};
    esp_err_t err = i2c_write_raw((const uint8_t[]){SSD1306_CTRL_CMD, win[0], win[1], win[2], win[3], win[4], win[5]}, 7);
    if (err != ESP_OK) {
        return err;
    }
    s_txbuf[0] = SSD1306_CTRL_DATA;
    memcpy(&s_txbuf[1], gfx_fb(), GFX_FB_SIZE);
    return i2c_write_raw(s_txbuf, sizeof(s_txbuf));
}

esp_err_t ssd1306_set_contrast(uint8_t contrast)
{
    return ssd1306_cmd2(0x81, contrast);
}

esp_err_t ssd1306_power(bool on)
{
    return ssd1306_cmd(on ? 0xAF : 0xAE);
}

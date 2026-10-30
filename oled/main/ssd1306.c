#include "ssd1306.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#include "font5x7.h"

static const char *TAG = "ssd1306";

/* ========================================================================== */
/* SSD1306 的 I2C 通信规则                                                     */
/*                                                                            */
/* 每次传输的第一个字节是「控制字节」：                                        */
/*   0x00 = 后面跟的是命令（D/C# = 0）                                        */
/*   0x40 = 后面跟的是显存数据（D/C# = 1）                                    */
/* 这是 SSD1306 I2C 接口的规定，也是它和 SPI 接口的主要差别。                 */
/* ========================================================================== */
#define SSD1306_CTRL_CMD  0x00
#define SSD1306_CTRL_DATA 0x40

static esp_err_t write_cmd(ssd1306_t *dev, uint8_t cmd)
{
    uint8_t buf[2] = {SSD1306_CTRL_CMD, cmd};
    return i2c_master_transmit(dev->i2c_dev, buf, sizeof(buf), -1);
}

/**
 * 初始化序列。顺序是有讲究的，几个关键点：
 *   - 0x20 0x02：页寻址模式（Page Addressing）。刷新时按「页」写，最简单。
 *   - 0x8D 0x14：打开电荷泵（charge pump）。**漏了这条屏幕不会亮**，
 *                这是新手最常见的"接线都对但屏幕全黑"的原因。
 *   - 0xA8 0x3F：多路复用比 = 高度 - 1（64 行 → 0x3F；32 行 → 0x1F）。
 *   - 0xDA 0x12：COM 引脚配置，128x64 用 0x12，128x32 用 0x02。
 */
static esp_err_t send_init_sequence(ssd1306_t *dev)
{
    static const uint8_t seq_begin[] = {
        0xAE,             /* display off（先关屏再配置）  */
        0x20, 0x02,       /* 页寻址模式                   */
        0xB0,             /* 页起始地址 = 0               */
        0xC8,             /* COM 扫描方向：反向（常用）   */
        0x00,             /* 列地址低 4 位 = 0            */
        0x10,             /* 列地址高 4 位 = 0            */
        0x40,             /* 显示起始行 = 0               */
        0x81, 0x7F,       /* 对比度（0x7F 中等亮度）      */
        0xA1,             /* 段重映射（左右不镜像）       */
        0xA6,             /* 正常显示（不反色）           */
        0xA4,             /* 显示内容跟随 GDDRAM          */
        0xD3, 0x00,       /* 显示偏移 = 0                 */
        0xD5, 0x80,       /* 时钟分频 / 振荡频率          */
        0xD9, 0xF1,       /* 预充电周期                   */
        0xDB, 0x40,       /* VCOMH 去选通电平             */
        0x8D, 0x14,       /* ★ 使能电荷泵（必须）         */
    };

    for (size_t i = 0; i < sizeof(seq_begin); i++) {
        ESP_RETURN_ON_ERROR(write_cmd(dev, seq_begin[i]), TAG, "cmd 0x%02X", seq_begin[i]);
    }

    /* 这两个命令取决于屏幕高度 */
    ESP_RETURN_ON_ERROR(write_cmd(dev, 0xA8), TAG, "mux");
    ESP_RETURN_ON_ERROR(write_cmd(dev, dev->height - 1), TAG, "mux value");
    ESP_RETURN_ON_ERROR(write_cmd(dev, 0xDA), TAG, "com pins");
    ESP_RETURN_ON_ERROR(write_cmd(dev, (dev->height == 64) ? 0x12 : 0x02), TAG, "com pins value");

    ESP_RETURN_ON_ERROR(write_cmd(dev, 0xAF), TAG, "display on");   /* 开屏 */
    return ESP_OK;
}

esp_err_t ssd1306_init(ssd1306_t *dev, i2c_port_t port, gpio_num_t sda_gpio,
                       gpio_num_t scl_gpio, uint8_t i2c_addr,
                       uint8_t width, uint8_t height)
{
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_INVALID_ARG, TAG, "dev is NULL");
    ESP_RETURN_ON_FALSE((height % 8) == 0 && height > 0,
                        ESP_ERR_INVALID_ARG, TAG, "height must be multiple of 8");
    ESP_RETURN_ON_FALSE(width <= 128, ESP_ERR_INVALID_ARG, TAG, "width must be <= 128");

    memset(dev, 0, sizeof(*dev));
    dev->addr   = i2c_addr;
    dev->width  = width;
    dev->height = height;

    /* ---- 1. 建立 I2C 主机总线（新版 driver/i2c_master.h API）---- */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port               = port,
        .sda_io_num             = sda_gpio,
        .scl_io_num             = scl_gpio,
        .clk_source             = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt      = 7,
        /* 开启内部上拉。多数 SSD1306 模块自带 4.7k 上拉，
           这里再开内部上拉只是双保险，不会冲突。 */
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &dev->bus), TAG, "i2c_new_master_bus failed");

    /* ---- 2. 在总线上挂一个设备（SSD1306）---- */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = i2c_addr,
        .scl_speed_hz    = 400000,      /* 400kHz 快速模式，SSD1306 完全够用 */
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(dev->bus, &dev_cfg, &dev->i2c_dev),
                        TAG, "i2c_master_bus_add_device failed (addr 0x%02X)", i2c_addr);

    /* ---- 3. 分配显存 ---- */
    size_t fb_size = (size_t)width * height / 8;
    dev->fb = calloc(fb_size, 1);
    ESP_RETURN_ON_FALSE(dev->fb != NULL, ESP_ERR_NO_MEM, TAG, "calloc fb failed");

    /* ---- 4. 发送初始化命令 ---- */
    esp_err_t ret = send_init_sequence(dev);
    if (ret != ESP_OK) {
        free(dev->fb);
        dev->fb = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "SSD1306 ready: %dx%d, I2C addr 0x%02X, SDA=GPIO%d SCL=GPIO%d",
             width, height, i2c_addr, (int)sda_gpio, (int)scl_gpio);
    return ESP_OK;
}

void ssd1306_deinit(ssd1306_t *dev)
{
    if (dev == NULL) {
        return;
    }
    free(dev->fb);
    dev->fb = NULL;
}

/**
 * 刷新：按「页」把显存推到屏幕。
 * SSD1306 的 GDDRAM 分成若干页，每页 8 行。写之前要先告诉它
 * 「我要写第几页、从第几列开始」，然后连续写数据即可，列地址会自动递增。
 */
esp_err_t ssd1306_refresh(ssd1306_t *dev)
{
    if (dev == NULL || dev->fb == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t pages = dev->height / 8;
    /* 控制字节 1 个 + 一整页的数据（width <= 128） */
    uint8_t buf[129];

    for (uint8_t p = 0; p < pages; p++) {
        ESP_RETURN_ON_ERROR(write_cmd(dev, 0xB0 | p), TAG, "set page");
        ESP_RETURN_ON_ERROR(write_cmd(dev, 0x00), TAG, "set col low");
        ESP_RETURN_ON_ERROR(write_cmd(dev, 0x10), TAG, "set col high");

        buf[0] = SSD1306_CTRL_DATA;
        memcpy(&buf[1], &dev->fb[(size_t)p * dev->width], dev->width);
        ESP_RETURN_ON_ERROR(i2c_master_transmit(dev->i2c_dev, buf, dev->width + 1, -1),
                            TAG, "page %d transmit failed", p);
    }
    return ESP_OK;
}

void ssd1306_clear(ssd1306_t *dev)
{
    if (dev == NULL || dev->fb == NULL) {
        return;
    }
    memset(dev->fb, 0x00, (size_t)dev->width * dev->height / 8);
}

/**
 * 画点。显存是「按页 + 按列」组织的：
 *   fb[(y / 8) * width + x] 的 bit(y % 8) 就代表像素 (x, y)。
 * 也就是每个字节竖直方向管 8 个像素，bit0 在最上面。
 */
void ssd1306_draw_pixel(ssd1306_t *dev, uint8_t x, uint8_t y, bool on)
{
    if (dev == NULL || dev->fb == NULL || x >= dev->width || y >= dev->height) {
        return;
    }
    uint8_t page = y / 8;
    uint8_t bit  = y % 8;
    uint8_t *p   = &dev->fb[(size_t)page * dev->width + x];

    if (on) {
        *p |= (1u << bit);
    } else {
        *p &= ~(1u << bit);
    }
}

void ssd1306_draw_char(ssd1306_t *dev, uint8_t x, uint8_t y, char c)
{
    /* 超出字库范围的字符统一画成 '?'，避免数组越界 */
    if (c < 32 || c > 126) {
        c = '?';
    }
    const uint8_t *glyph = font5x7[(uint8_t)c - 32];

    for (uint8_t row = 0; row < FONT5X7_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (uint8_t col = 0; col < FONT5X7_WIDTH; col++) {
            /* bit4 对应最左像素，所以是 (4 - col) */
            if (bits & (1u << (4 - col))) {
                ssd1306_draw_pixel(dev, x + col, y + row, true);
            }
        }
    }
}

void ssd1306_draw_string(ssd1306_t *dev, uint8_t x, uint8_t y, const char *str)
{
    if (dev == NULL || str == NULL) {
        return;
    }
    uint8_t cursor = x;
    for (const char *p = str; *p != '\0'; p++) {
        /* 字符宽 5 + 1 像素间隔 = 6 像素步进 */
        if (cursor + FONT5X7_WIDTH > dev->width) {
            break;   /* 超出右边界就停，不做自动换行 */
        }
        ssd1306_draw_char(dev, cursor, y, *p);
        cursor += FONT5X7_WIDTH + 1;
    }
}

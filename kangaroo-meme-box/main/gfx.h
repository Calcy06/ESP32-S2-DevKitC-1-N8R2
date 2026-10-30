#pragma once

/**
 * 纯软件图形库（不依赖 ESP-IDF，可在 PC 上编译预览）
 * ---------------------------------------------------------------
 * 1bpp 帧缓冲，128x64，共 1024 字节。
 * 坐标原点在左上角，x 向右，y 向下。
 * 所有图元自动裁剪到屏幕范围。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_WIDTH   128
#define GFX_HEIGHT  64
#define GFX_FB_SIZE (GFX_WIDTH * GFX_HEIGHT / 8)

/* 绘制颜色 */
#define COL_BLACK 0  /* 熄灭 */
#define COL_WHITE 1  /* 点亮 */
#define COL_XOR   2  /* 反色 */

/** @brief 取帧缓冲指针（1024 字节，可直接发给 SSD1306） */
uint8_t *gfx_fb(void);

void gfx_clear(void);

void gfx_pixel(int x, int y, uint8_t color);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);
void gfx_line(int x0, int y0, int x1, int y1, uint8_t color);

void gfx_rect(int x, int y, int w, int h, uint8_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);

void gfx_circle(int cx, int cy, int r, uint8_t color);
void gfx_fill_circle(int cx, int cy, int r, uint8_t color);

void gfx_ellipse(int cx, int cy, int rx, int ry, uint8_t color);
void gfx_fill_ellipse(int cx, int cy, int rx, int ry, uint8_t color);

void gfx_round_rect(int x, int y, int w, int h, int r, uint8_t color);
void gfx_fill_round_rect(int x, int y, int w, int h, int r, uint8_t color);

/** @brief 1bpp 位图，行优先，字节高位在左 */
void gfx_bitmap(int x, int y, int w, int h, const uint8_t *bmp, uint8_t color);

/**
 * @brief 绘制 ASCII 字符串
 * @param scale 整数放大倍数（1 = 5x7，2 = 10x14）
 * @return 实际绘制宽度（像素）
 */
int gfx_text(int x, int y, const char *ascii, uint8_t color, uint8_t scale);
int gfx_measure_ascii(const char *ascii, uint8_t scale);

/**
 * @brief 绘制 UTF-8 字符串（中文用 16x16 点阵，ASCII 用 5x7）
 * @return 实际绘制宽度（像素）
 */
int gfx_text_cn(int x, int y, const char *utf8, uint8_t color, uint8_t scale);
int gfx_measure_cn(const char *utf8);

#ifdef __cplusplus
}
#endif

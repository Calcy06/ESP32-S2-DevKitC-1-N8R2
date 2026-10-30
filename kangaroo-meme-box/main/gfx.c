#include "gfx.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "font_ascii.h"
#include "font_cn.h"

static uint8_t s_fb[GFX_FB_SIZE];

uint8_t *gfx_fb(void)
{
    return s_fb;
}

void gfx_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

/* ================================================================== 图元 */

void gfx_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) {
        return;
    }
    int idx = (y >> 3) * GFX_WIDTH + x;
    uint8_t mask = (uint8_t)(1 << (y & 7));
    switch (color) {
    case COL_WHITE: s_fb[idx] |= mask; break;
    case COL_BLACK: s_fb[idx] &= (uint8_t)~mask; break;
    default:        s_fb[idx] ^= mask; break;
    }
}

void gfx_hline(int x, int y, int w, uint8_t color)
{
    if (y < 0 || y >= GFX_HEIGHT) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w;
    if (x1 > GFX_WIDTH) {
        x1 = GFX_WIDTH;
    }
    for (int i = x0; i < x1; i++) {
        gfx_pixel(i, y, color);
    }
}

void gfx_vline(int x, int y, int h, uint8_t color)
{
    if (x < 0 || x >= GFX_WIDTH) {
        return;
    }
    int y0 = y < 0 ? 0 : y;
    int y1 = y + h;
    if (y1 > GFX_HEIGHT) {
        y1 = GFX_HEIGHT;
    }
    for (int i = y0; i < y1; i++) {
        gfx_pixel(x, i, color);
    }
}

void gfx_line(int x0, int y0, int x1, int y1, uint8_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int e = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = e * 2;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

void gfx_rect(int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    /* 逐像素写入，配合 gfx_pixel 的裁剪，逻辑简单不易出错 */
    int ys = y < 0 ? 0 : y;
    int ye = y + h > GFX_HEIGHT ? GFX_HEIGHT : y + h;
    for (int j = ys; j < ye; j++) {
        gfx_hline(x, j, w, color);
    }
}

void gfx_circle(int cx, int cy, int r, uint8_t color)
{
    if (r <= 0) {
        return;
    }
    int x = r, y = 0, err = 0;
    while (x >= y) {
        gfx_pixel(cx + x, cy + y, color);
        gfx_pixel(cx + y, cy + x, color);
        gfx_pixel(cx - y, cy + x, color);
        gfx_pixel(cx - x, cy + y, color);
        gfx_pixel(cx - x, cy - y, color);
        gfx_pixel(cx - y, cy - x, color);
        gfx_pixel(cx + y, cy - x, color);
        gfx_pixel(cx + x, cy - y, color);
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void gfx_fill_circle(int cx, int cy, int r, uint8_t color)
{
    if (r <= 0) {
        return;
    }
    int x = r, y = 0, err = 0;
    while (x >= y) {
        gfx_hline(cx - x, cy + y, 2 * x + 1, color);
        gfx_hline(cx - x, cy - y, 2 * x + 1, color);
        gfx_hline(cx - y, cy + x, 2 * y + 1, color);
        gfx_hline(cx - y, cy - x, 2 * y + 1, color);
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void gfx_ellipse(int cx, int cy, int rx, int ry, uint8_t color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    /* 参数方程描点，步数按周长估算，保证线条连续 */
    int n = (int)((float)(rx + ry) * 3.4f);
    if (n < 16) {
        n = 16;
    }
    for (int i = 0; i < n; i++) {
        float a = 6.2831853f * (float)i / (float)n;
        gfx_pixel(cx + (int)lroundf((float)rx * cosf(a)),
                  cy + (int)lroundf((float)ry * sinf(a)), color);
    }
}

void gfx_fill_ellipse(int cx, int cy, int rx, int ry, uint8_t color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    for (int dy = -ry; dy <= ry; dy++) {
        float t = 1.0f - ((float)dy * (float)dy) / ((float)ry * (float)ry);
        if (t < 0.0f) {
            continue;
        }
        int dx = (int)sqrtf(t * (float)rx * (float)rx);
        gfx_hline(cx - dx, cy + dy, 2 * dx + 1, color);
    }
}

/* 四分之一圆（用于圆角），quad: 0=左上 1=右上 2=左下 3=右下 */
static void corner_arc(int cx, int cy, int r, int quad, uint8_t color)
{
    int sx = (quad == 0 || quad == 2) ? -1 : 1;
    int sy = (quad < 2) ? -1 : 1;
    int steps = r * 4 + 8;
    for (int i = 0; i <= steps; i++) {
        float a = 1.5707963f * (float)i / (float)steps;
        gfx_pixel(cx + sx * (int)lroundf((float)r * cosf(a)),
                  cy + sy * (int)lroundf((float)r * sinf(a)), color);
    }
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint8_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    gfx_hline(x + r, y, w - 2 * r, color);
    gfx_hline(x + r, y + h - 1, w - 2 * r, color);
    gfx_vline(x, y + r, h - 2 * r, color);
    gfx_vline(x + w - 1, y + r, h - 2 * r, color);
    corner_arc(x + r, y + r, r, 0, color);
    corner_arc(x + w - 1 - r, y + r, r, 1, color);
    corner_arc(x + r, y + h - 1 - r, r, 2, color);
    corner_arc(x + w - 1 - r, y + h - 1 - r, r, 3, color);
}

void gfx_fill_round_rect(int x, int y, int w, int h, int r, uint8_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    gfx_fill_rect(x + r, y, w - 2 * r, h, color);
    gfx_fill_rect(x, y + r, r, h - 2 * r, color);
    gfx_fill_rect(x + w - r, y + r, r, h - 2 * r, color);
    gfx_fill_circle(x + r, y + r, r, color);
    gfx_fill_circle(x + w - 1 - r, y + r, r, color);
    gfx_fill_circle(x + r, y + h - 1 - r, r, color);
    gfx_fill_circle(x + w - 1 - r, y + h - 1 - r, r, color);
}

void gfx_bitmap(int x, int y, int w, int h, const uint8_t *bmp, uint8_t color)
{
    int stride = (w + 7) / 8;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (bmp[j * stride + (i >> 3)] & (0x80 >> (i & 7))) {
                gfx_pixel(x + i, y + j, color);
            }
        }
    }
}

/* ================================================================== 文本 */

int gfx_text(int x, int y, const char *ascii, uint8_t color, uint8_t scale)
{
    if (ascii == NULL) {
        return 0;
    }
    if (scale == 0) {
        scale = 1;
    }
    int cx = x;
    for (const char *p = ascii; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x20 || ch > 0x7E) {
            ch = '?';
        }
        const uint8_t *g = ASCII_FONT[ch - 0x20];
        for (int col = 0; col < ASCII_W; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < ASCII_H; row++) {
                if (bits & (1 << row)) {
                    if (scale == 1) {
                        gfx_pixel(cx + col, y + row, color);
                    } else {
                        gfx_fill_rect(cx + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
        }
        cx += ASCII_ADV * scale;
    }
    return cx - x;
}

int gfx_measure_ascii(const char *ascii, uint8_t scale)
{
    if (ascii == NULL) {
        return 0;
    }
    if (scale == 0) {
        scale = 1;
    }
    int n = 0;
    while (ascii[n]) {
        n++;
    }
    return n * ASCII_ADV * scale;
}

static int utf8_next(const char *p, uint32_t *cp)
{
    const unsigned char *s = (const unsigned char *)p;
    if (s[0] < 0x80) {
        *cp = s[0];
        return 1;
    }
    if ((s[0] & 0xE0) == 0xC0 && s[1]) {
        *cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
        *cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        *cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
              ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    *cp = '?';
    return 1;
}

static const uint8_t *find_cn_glyph(const char *p, int len)
{
    for (int i = 0; i < CN_FONT_COUNT; i++) {
        if (strncmp(CN_FONT[i].utf8, p, (size_t)len) == 0 && CN_FONT[i].utf8[len] == '\0') {
            return CN_FONT[i].data;
        }
    }
    return NULL;
}

int gfx_text_cn(int x, int y, const char *utf8, uint8_t color, uint8_t scale)
{
    if (utf8 == NULL) {
        return 0;
    }
    if (scale == 0) {
        scale = 1;
    }
    const int S = CN_FONT_SIZE;
    int cx = x;
    for (const char *p = utf8; *p;) {
        uint32_t cp;
        int len = utf8_next(p, &cp);
        if (len == 1 && cp >= 0x20 && cp <= 0x7E) {
            char tmp[2] = {(char)cp, 0};
            cx += gfx_text(cx, y + (S - ASCII_H) / 2 * scale, tmp, color, scale);
            p += len;
            continue;
        }
        const uint8_t *g = find_cn_glyph(p, len);
        if (g == NULL) {
            /* 缺字：空心方框，方便一眼发现字库没覆盖 */
            gfx_rect(cx + 2 * scale, y + 2 * scale, (S - 4) * scale, (S - 4) * scale, color);
        } else {
            for (int row = 0; row < S; row++) {
                for (int col = 0; col < S; col++) {
                    int idx = row * CN_FONT_ROW_BYTES + (col >> 3);
                    if (g[idx] & (0x80 >> (col & 7))) {
                        if (scale == 1) {
                            gfx_pixel(cx + col, y + row, color);
                        } else {
                            gfx_fill_rect(cx + col * scale, y + row * scale, scale, scale, color);
                        }
                    }
                }
            }
        }
        cx += S * scale;
        p += len;
    }
    return cx - x;
}

int gfx_measure_cn(const char *utf8)
{
    if (utf8 == NULL) {
        return 0;
    }
    int w = 0;
    for (const char *p = utf8; *p;) {
        uint32_t cp;
        int len = utf8_next(p, &cp);
        w += (len == 1 && cp >= 0x20 && cp <= 0x7E) ? ASCII_ADV : CN_FONT_SIZE;
        p += len;
    }
    return w;
}

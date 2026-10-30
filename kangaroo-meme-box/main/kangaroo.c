#include "kangaroo.h"

#include <math.h>
#include <stddef.h>
#include "gfx.h"

/*
 * 逻辑坐标系（scale=1 时）：
 *   头心 (0,-8) rx=9 ry=8 ；身体 (0,+4) rx=11 ry=9 ；脚底 y=+16.5
 *   耳尖 y=-21.5 ；尾巴尖 x=-22 ；整体约 40 x 38 像素
 *
 * 画法要点：全部是"先白后黑"的叠加——白色剪影打底，黑色挖出五官和
 * 箱子轮廓，单色屏上就能出层次。
 */
#define K_W 40
#define K_H 39

static struct {
    int ox, oy, s;
} C;

static inline int X(int x) { return C.ox + x * C.s; }
static inline int Y(int y) { return C.oy + y * C.s; }
static inline int L(int v) { return v * C.s; }

/* 棋盘格填充的椭圆，单色屏上当 50% 灰用 */
static void shadow_ellipse(int cx, int cy, int rx, int ry)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    for (int y = -ry; y <= ry; y++) {
        for (int x = -rx; x <= rx; x++) {
            float d = (float)(x * x) / (float)(rx * rx) + (float)(y * y) / (float)(ry * ry);
            if (d <= 1.0f && (((x + y) & 1) == 0)) {
                gfx_pixel(cx + x, cy + y, COL_WHITE);
            }
        }
    }
}

int kangaroo_width(int scale)
{
    return (scale ? scale : 1) * K_W;
}

int kangaroo_height(int scale)
{
    return (scale ? scale : 1) * K_H;
}

/* ------------------------------------------------------------------ 部件 */

static void draw_tail(void)
{
    /* 尾巴从臀部向下后方甩出去，再收细上翘 */
    const int n = 12;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        int x = -8 - (int)(14.0f * t);
        int y = 11 + (int)(4.0f * sinf(3.14159f * t * 0.85f));
        int r = (int)(4.0f - 2.6f * t);
        if (r < 1) {
            r = 1;
        }
        gfx_fill_circle(X(x), Y(y), L(r), COL_WHITE);
    }
}

static void draw_ears(int hx, int hy)
{
    /* 细长耳朵，是袋鼠相对其他动物最好认的特征 */
    gfx_fill_ellipse(X(hx - 5), Y(hy - 6), L(3), L(7), COL_WHITE);
    gfx_fill_ellipse(X(hx + 5), Y(hy - 7), L(3), L(7), COL_WHITE);
    /* 内耳要短、要靠上，否则会和眼睛连成一条竖线，脸就毁了 */
    gfx_fill_ellipse(X(hx - 5), Y(hy - 9), L(1), L(3), COL_BLACK);
    gfx_fill_ellipse(X(hx + 5), Y(hy - 10), L(1), L(3), COL_BLACK);
}

static void draw_face(const kangaroo_t *k, int hx, int hy)
{
    const int ex = 5, ey = -2;   /* 眼睛相对头心：间距拉开，避免和鼻子糊在一起 */

    for (int side = -1; side <= 1; side += 2) {
        int x = hx + side * ex;
        int y = hy + ey;

        if (k->blink) {
            gfx_hline(X(x - 2), Y(y), L(4), COL_BLACK);
        } else if (k->mood == MOOD_SMUG) {
            /* 眯眼：一条平线 + 两端下勾 */
            gfx_hline(X(x - 2), Y(y), L(4), COL_BLACK);
            gfx_pixel(X(x - 2), Y(y + 1), COL_BLACK);
            gfx_pixel(X(x + 1), Y(y + 1), COL_BLACK);
        } else {
            /* 圆豆眼：单色屏上高光会把眼睛打碎，干脆纯黑 */
            gfx_fill_circle(X(x), Y(y), L(2), COL_BLACK);
        }
    }

    /* 鼻子：一条 3px 短横线。和眼睛、嘴都要留出空行，否则竖着连成一条线 */
    gfx_hline(X(hx - 1), Y(hy + 4), L(3), COL_BLACK);

    /* 嘴 */
    if (k->mood == MOOD_SMUG) {
        gfx_line(X(hx - 3), Y(hy + 5), X(hx + 2), Y(hy + 6), COL_BLACK);
        gfx_line(X(hx + 2), Y(hy + 6), X(hx + 4), Y(hy + 4), COL_BLACK);
    } else if (k->mouth > 0) {
        gfx_fill_ellipse(X(hx), Y(hy + 4 + k->mouth / 2), L(1 + k->mouth / 2), L(k->mouth), COL_BLACK);
    } else {
        gfx_pixel(X(hx), Y(hy + 5), COL_BLACK);
        gfx_line(X(hx), Y(hy + 5), X(hx - 2), Y(hy + 6), COL_BLACK);
        gfx_line(X(hx), Y(hy + 5), X(hx + 2), Y(hy + 6), COL_BLACK);
    }
}

static void draw_box(int x, int y)
{
    /* 外卖箱：白底黑框，盖子一条线 */
    const int w = 10, h = 9;
    gfx_fill_rect(X(x), Y(y), L(w), L(h), COL_WHITE);
    gfx_rect(X(x), Y(y), L(w), L(h), COL_BLACK);
    gfx_hline(X(x), Y(y + 3), L(w), COL_BLACK);
    /* 提手 */
    gfx_line(X(x + 2), Y(y - 2), X(x + 8), Y(y - 2), COL_BLACK);
    gfx_line(X(x + 2), Y(y - 2), X(x + 2), Y(y), COL_BLACK);
    gfx_line(X(x + 8), Y(y - 2), X(x + 8), Y(y), COL_BLACK);
}

/* ------------------------------------------------------------------ 主绘制 */

void kangaroo_draw(const kangaroo_t *k)
{
    if (k == NULL) {
        return;
    }
    C.s = k->scale > 0 ? k->scale : 1;
    C.ox = k->cx + k->tilt;
    C.oy = k->cy + k->bounce;

    const int sq = k->squash;
    const int hx = k->tilt;               /* 头比身体多晃，显得更"欠" */
    const int hy = -8 + sq / 2;

    /* 地面影子 */
    {
        int shrink = k->bounce < 0 ? (-k->bounce / 3) : 0;
        shadow_ellipse(X(0), Y(18), L(11 - shrink), L(2));
    }

    draw_tail();

    /* 脚 */
    gfx_fill_ellipse(X(-6), Y(13), L(6), L(3), COL_WHITE);
    gfx_fill_ellipse(X(6), Y(13), L(6), L(3), COL_WHITE);

    /* 身体（呼吸时变胖变矮） */
    gfx_fill_ellipse(X(0), Y(4), L(11 + sq), L(9 - sq), COL_WHITE);

    draw_ears(hx, hy);
    gfx_fill_ellipse(X(hx), Y(hy), L(9 + sq / 2), L(8 + sq / 2), COL_WHITE);

    /* 手臂 */
    gfx_fill_circle(X(-10), Y(2), L(3), COL_WHITE);
    gfx_fill_circle(X(10), Y(2), L(3), COL_WHITE);

    if (k->hold_box) {
        draw_box(10, 3);
    }

    draw_face(k, hx, hy);

    if (k->mood == MOOD_ANGRY) {
        /* 头顶右边的怒气符号（两条斜杠），比眉毛更不容易糊 */
        int bx = hx + 11, by = hy - 6;
        gfx_line(X(bx), Y(by), X(bx + 2), Y(by + 4), COL_WHITE);
        gfx_line(X(bx + 4), Y(by - 1), X(bx + 6), Y(by + 3), COL_WHITE);
    }
}

/*
 * PC 端预览工具 —— 不需要开发板，把 gfx 帧缓冲导出成 PBM 图片
 *
 * 编译:
 *   gcc -O2 -I../main preview_host.c ../main/gfx.c ../main/kangaroo.c -lm -o preview
 * 运行:
 *   ./preview out            # 生成 out_1boot.pbm ... 等 4 帧
 *   ./preview out 4          # 每像素放大 4 倍
 * 转成 png:
 *   python3 -c "from PIL import Image; import glob; \
 *     [Image.open(f).convert('RGB').save(f[:-4]+'.png') for f in glob.glob('out_*.pbm')]"
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gfx.h"
#include "kangaroo.h"

static void dump_pbm(const char *path, int scale)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    /* PBM P1: 0 = 白, 1 = 黑。点亮(bit=1)要显示成白，所以输出 '0' */
    fprintf(f, "P1\n%d %d\n", GFX_WIDTH * scale, GFX_HEIGHT * scale);
    for (int y = 0; y < GFX_HEIGHT * scale; y++) {
        for (int x = 0; x < GFX_WIDTH * scale; x++) {
            int sx = x / scale, sy = y / scale;
            int idx = (sy >> 3) * GFX_WIDTH + sx;
            int bit = (gfx_fb()[idx] >> (sy & 7)) & 1;
            fputc(bit ? '0' : '1', f);
            fputc(' ', f);
        }
        fputc('\n', f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *prefix = argc > 1 ? argv[1] : "frame";
    int scale = argc > 2 ? atoi(argv[2]) : 4;
    char name[128];

    /* 基准姿态，后面的帧只改必要的字段 */
#define RENDER(subtitle, tag)                                        \
    do {                                                             \
        gfx_clear();                                                 \
        kangaroo_draw(&k);                                           \
        for (int x = 0; x < GFX_WIDTH; x += 2) {                     \
            gfx_pixel(x, 44, COL_WHITE);                             \
        }                                                            \
        int w = gfx_measure_cn(subtitle);                            \
        gfx_text_cn((GFX_WIDTH - w) / 2, 46, subtitle, COL_WHITE, 1); \
        snprintf(name, sizeof(name), "%s_%s.pbm", prefix, tag);      \
        dump_pbm(name, scale);                                       \
    } while (0)

    kangaroo_t k = {
        .cx = 64 + 2, .cy = 26, .scale = 1,
        .bounce = 0, .squash = 0, .tilt = 0,
        .blink = false, .mouth = 0,
        .mood = MOOD_IDLE, .hold_box = true,
    };

    /* 1) 开机：蹦两下 */
    k.bounce = -4;
    k.squash = 1;
    RENDER("单片机可真好玩", "1boot");

    /* 2) 待机：呼吸 + 睁眼 */
    k.bounce = 0;
    k.squash = 0;
    RENDER("你胆子真是", "2idle");

    /* 3) 说台词：张嘴 + 抖 */
    k.mood = MOOD_SMUG;
    k.mouth = 3;
    k.tilt = 1;
    k.squash = 1;
    RENDER("肥嘟嘟的", "3smug");

    /* 4) 生气：跳起来 + 怒气符号 */
    k.mood = MOOD_ANGRY;
    k.mouth = 0;
    k.tilt = -1;
    k.bounce = -4;
    k.squash = 0;
    RENDER("敢抢我的外卖", "4angry");

#undef RENDER
    printf("已生成 %s_1boot.pbm / _2idle.pbm / _3smug.pbm / _4angry.pbm (放大 %dx)\n", prefix, scale);
    return 0;
}

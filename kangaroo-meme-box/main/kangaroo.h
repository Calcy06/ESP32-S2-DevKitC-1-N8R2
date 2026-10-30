#pragma once

/**
 * 圆滚滚袋鼠 —— 程序化绘制，不占 Flash（没有一张图片）
 * ---------------------------------------------------------------
 * 全部由椭圆/圆/线组合而成，所以可以任意缩放、任意加动作参数，
 * 换姿势只要改几个整数，不用重新导出图片。
 *
 * 坐标系：以 (cx, cy) 为袋鼠身体中心，内部用逻辑坐标（-24..+24 左右），
 * 乘以 scale 后映射到屏幕，便于整体缩放。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOOD_IDLE = 0,  /* 发呆，时不时眨眼 */
    MOOD_TALK,      /* 说话，嘴巴开合 */
    MOOD_SMUG,      /* 嚣张，眯眼 + 斜嘴 + 抖腿 */
    MOOD_ANGRY,     /* 生气，眉压 + 怒气符号 */
} kangaroo_mood_t;

typedef struct {
    int cx, cy;        /* 身体中心屏幕坐标 */
    int scale;         /* 缩放倍数，1 = 约 46x45 像素，0 视为 1 */
    int bounce;        /* 上下偏移(负=跳起)，单位像素 */
    int squash;        /* 呼吸挤压：-2..+2，正=变胖 */
    int tilt;          /* 左右摇摆：-3..+3 像素 */
    bool blink;        /* 闭眼 */
    int mouth;         /* 嘴张开度 0..4 */
    kangaroo_mood_t mood;
    bool hold_box;     /* 手里拎外卖箱 */
} kangaroo_t;

/** @brief 按当前姿态绘制袋鼠（会覆盖绘制区域的像素） */
void kangaroo_draw(const kangaroo_t *k);

/** @brief 袋鼠包围盒宽度(像素)，用于居中排版 */
int kangaroo_width(int scale);

/** @brief 袋鼠包围盒高度(像素) */
int kangaroo_height(int scale);

#ifdef __cplusplus
}
#endif

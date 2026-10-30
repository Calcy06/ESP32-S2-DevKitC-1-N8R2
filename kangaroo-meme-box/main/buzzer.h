#pragma once

/**
 * 有源蜂鸣器驱动（高电平触发）
 * ---------------------------------------------------------------
 * 有源蜂鸣器内部自带振荡源，GPIO 给高电平就响、给低电平就停，
 * 音高固定，能控制的只有"响多久 / 停多久"——正好拿来打节奏。
 *
 * 为避免阻塞动画，这里用非阻塞状态机：主循环每帧调用 buzzer_tick()，
 * 到点自动切换开关，不需要 vTaskDelay 卡住整个任务。
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 一个"音符"：响 on_ms，停 off_ms。on_ms=0 表示纯休止 */
typedef struct {
    uint16_t on_ms;
    uint16_t off_ms;
} buzz_note_t;

/**
 * @brief 初始化蜂鸣器 GPIO
 * @param gpio 输出引脚（高电平 = 响）
 */
esp_err_t buzzer_init(int gpio);

/** @brief 立即开/关（手动控制） */
void buzzer_set(bool on);

/** @brief 播放节奏序列（非阻塞，会打断正在播的） */
void buzzer_play(const buzz_note_t *notes, int count);

/** @brief 停止并静音 */
void buzzer_stop(void);

/** @brief 主循环里周期调用，推进节奏状态机 */
void buzzer_tick(void);

/** @brief 是否正在播放 */
bool buzzer_busy(void);

/** @brief 全局静音开关（静音时不驱动 GPIO） */
void buzzer_mute(bool mute);
bool buzzer_is_muted(void);

/* ------------------------------------------------------- 现成的节奏预设 */

/** 短促提示音 "嘀" */
extern const buzz_note_t BUZZ_TICK;
/** 两短声 "嘀嘀" */
extern const buzz_note_t BUZZ_DOUBLE[2];
/** 三段式开机音 */
extern const buzz_note_t BUZZ_BOOT[3];
/** 报警音：急促连响 6 声 */
extern const buzz_note_t BUZZ_ALARM[6];

/**
 * 台词节奏：「你 胆 子 真 是 肥 嘟 嘟 的」
 * 9 个音节按中文语流长短排布，听起来像电报码版的台词
 */
extern const buzz_note_t BUZZ_LINE_SMUG[9];
/** 「敢 抢 我 的 外 卖」5 个音节，语气更冲 */
extern const buzz_note_t BUZZ_LINE_ANGRY[5];
/** 「单 片 机 可 真 好 玩」7 个音节 */
extern const buzz_note_t BUZZ_LINE_FUN[7];

#ifdef __cplusplus
}
#endif

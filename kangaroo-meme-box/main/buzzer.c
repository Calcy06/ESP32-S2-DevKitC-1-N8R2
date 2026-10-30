#include "buzzer.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "buzzer";

static int s_gpio = -1;
static bool s_muted;
static bool s_playing;
static bool s_phase_on;
static const buzz_note_t *s_notes;
static int s_count;
static int s_idx;
static int64_t s_next_us;

/* ------------------------------------------------------------------ 预设 */

const buzz_note_t BUZZ_TICK = {30, 0};

const buzz_note_t BUZZ_DOUBLE[2] = {
    {40, 50}, {40, 0},
};

const buzz_note_t BUZZ_BOOT[3] = {
    {60, 60}, {60, 60}, {90, 0},
};

const buzz_note_t BUZZ_ALARM[6] = {
    {70, 60}, {70, 60}, {70, 60}, {70, 60}, {70, 60}, {120, 0},
};

/*
 * 台词节奏：把每个字的发音长度粗略类比成音符时长。
 * 四声/入声短促，轻声拖长，整体读起来有"人说话"的顿挫感。
 */
const buzz_note_t BUZZ_LINE_SMUG[9] = {
    {70,  55},  /* 你 */
    {85,  60},  /* 胆 */
    {60,  70},  /* 子 */
    {75,  55},  /* 真 */
    {75,  60},  /* 是 */
    {95,  60},  /* 肥 */
    {55,  45},  /* 嘟 */
    {55,  45},  /* 嘟 */
    {150,  0},  /* 的 —— 拖长收尾 */
};

const buzz_note_t BUZZ_LINE_ANGRY[5] = {
    {110, 45},  /* 敢 */
    {70,  40},  /* 抢 */
    {80,  45},  /* 我 */
    {70,  40},  /* 的 */
    {140,  0},  /* 卖 */
};

const buzz_note_t BUZZ_LINE_FUN[7] = {
    {65, 55}, {55, 50}, {75, 55}, {70, 60}, {60, 55}, {60, 50}, {130, 0},
};

/* ------------------------------------------------------------------ 实现 */

static inline void drive(bool on)
{
    if (s_gpio < 0) {
        return;
    }
    gpio_set_level((gpio_num_t)s_gpio, (on && !s_muted) ? 1 : 0);
}

esp_err_t buzzer_init(int gpio)
{
    s_gpio = gpio;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, /* 悬空时默认不响 */
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }
    drive(false);
    ESP_LOGI(TAG, "buzzer on GPIO%d (active-high)", gpio);
    return ESP_OK;
}

void buzzer_set(bool on)
{
    s_playing = false;
    drive(on);
}

void buzzer_play(const buzz_note_t *notes, int count)
{
    if (notes == NULL || count <= 0) {
        return;
    }
    s_notes = notes;
    s_count = count;
    s_idx = 0;
    s_playing = true;
    s_phase_on = true;
    drive(true);
    s_next_us = esp_timer_get_time() + (int64_t)notes[0].on_ms * 1000;
}

void buzzer_stop(void)
{
    s_playing = false;
    drive(false);
}

bool buzzer_busy(void)
{
    return s_playing;
}

void buzzer_mute(bool mute)
{
    s_muted = mute;
    if (mute) {
        drive(false);
    }
}

bool buzzer_is_muted(void)
{
    return s_muted;
}

void buzzer_tick(void)
{
    if (!s_playing) {
        return;
    }
    int64_t now = esp_timer_get_time();
    if (now < s_next_us) {
        return;
    }
    if (s_phase_on) {
        /* 响的阶段结束 -> 进入静音间隙 */
        drive(false);
        s_phase_on = false;
        s_next_us = now + (int64_t)s_notes[s_idx].off_ms * 1000;
        /* off 为 0 时立刻进入下一音符 */
        if (s_notes[s_idx].off_ms == 0) {
            s_idx++;
            if (s_idx >= s_count) {
                s_playing = false;
                return;
            }
            s_phase_on = true;
            drive(true);
            s_next_us = now + (int64_t)s_notes[s_idx].on_ms * 1000;
        }
    } else {
        s_idx++;
        if (s_idx >= s_count) {
            s_playing = false;
            drive(false);
            return;
        }
        s_phase_on = true;
        drive(true);
        s_next_us = now + (int64_t)s_notes[s_idx].on_ms * 1000;
    }
}

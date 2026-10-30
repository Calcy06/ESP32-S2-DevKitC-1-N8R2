#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"

#include "board_config.h"
#include "ssd1306.h"
#include "gfx.h"
#include "kangaroo.h"
#include "buzzer.h"

static const char *TAG = "meme-box";

#define OLED_SDA   CONFIG_OLED_SDA_GPIO
#define OLED_SCL   CONFIG_OLED_SCL_GPIO
#define OLED_ADDR  CONFIG_OLED_I2C_ADDR
#define BZR_GPIO   CONFIG_BUZZER_GPIO
#define BTN_GPIO   CONFIG_BUTTON_GPIO
#define FRAME_MS   CONFIG_FRAME_INTERVAL_MS

/* 屏幕分区：上 45 行动画，下面 16 行字幕，中间一条分隔线 */
#define SUBTITLE_Y 46

typedef enum {
    SCENE_BOOT = 0,
    SCENE_IDLE,
    SCENE_LINE,   /* 正在播台词 */
    SCENE_ALARM,  /* 报警：急促蜂鸣 + 抖动（预留，接传感器时用） */
} scene_t;

typedef struct {
    const char *text;              /* 字幕 */
    const buzz_note_t *notes;      /* 蜂鸣节奏 */
    int notes_len;
    kangaroo_mood_t mood;
    bool hold_box;
} line_t;

static const line_t LINES[] = {
    {
        .text = "你胆子真是肥嘟嘟的",
        .notes = BUZZ_LINE_SMUG, .notes_len = 9,
        .mood = MOOD_SMUG, .hold_box = true,
    },
    {
        .text = "敢抢我的外卖",
        .notes = BUZZ_LINE_ANGRY, .notes_len = 5,
        .mood = MOOD_ANGRY, .hold_box = true,
    },
    {
        .text = "单片机可真好玩",
        .notes = BUZZ_LINE_FUN, .notes_len = 7,
        .mood = MOOD_TALK, .hold_box = false,
    },
};
#define LINE_COUNT (int)(sizeof(LINES) / sizeof(LINES[0]))

/* ------------------------------------------------------------------ 状态 */

static scene_t s_scene = SCENE_BOOT;
static int64_t s_scene_start;      /* ms */
static int s_line_idx;
static bool s_muted;
static int64_t s_line_end;         /* SCENE_LINE 结束时间(ms) */
static int64_t s_next_blink;       /* ms */
static int64_t s_blink_until;      /* ms */
static int64_t s_next_auto;        /* ms, 待机自动播放 */

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void set_scene(scene_t s, int64_t now)
{
    s_scene = s;
    s_scene_start = now;
}

static int line_duration_ms(const line_t *l)
{
    int total = 0;
    for (int i = 0; i < l->notes_len; i++) {
        total += l->notes[i].on_ms + l->notes[i].off_ms;
    }
    return total + 200;   /* 末尾留一点余韵 */
}

/* ------------------------------------------------------------------ 字幕 */

/*
 * 字幕条：16px 高的汉字一行最多放 8 个，超长就横向滚动。
 * 先停 PAUSE_MS，再用 SLIDE_MS 匀速滑过去，循环。
 */
#define MARQUEE_PAUSE_MS 900
#define MARQUEE_SLIDE_MS 2600

static int marquee_x(int text_w, int64_t t)
{
    if (text_w <= GFX_WIDTH) {
        return (GFX_WIDTH - text_w) / 2;   /* 短句居中 */
    }
    int over = text_w - GFX_WIDTH;
    int cycle = MARQUEE_PAUSE_MS + MARQUEE_SLIDE_MS + MARQUEE_PAUSE_MS;
    int p = (int)((t - s_scene_start) % cycle);

    if (p < MARQUEE_PAUSE_MS) {
        return 0;                                  /* 停在最左 */
    }
    p -= MARQUEE_PAUSE_MS;
    if (p < MARQUEE_SLIDE_MS) {
        return -(over * p) / MARQUEE_SLIDE_MS;     /* 往左滑 */
    }
    return -over;                                  /* 停在最右 */
}

/* ------------------------------------------------------------------ 渲染 */

static void draw_divider(void)
{
    /* 分隔线用点线，比实线更"字幕条" */
    for (int x = 0; x < GFX_WIDTH; x += 2) {
        gfx_pixel(x, 44, COL_WHITE);
    }
}

static void draw_mute_icon(void)
{
    /* 右上角：小喇叭 + 一道斜杠 */
    gfx_fill_rect(112, 2, 3, 5, COL_WHITE);
    gfx_fill_rect(115, 3, 2, 3, COL_WHITE);
    gfx_line(110, 1, 118, 9, COL_BLACK);
}

static void render(int64_t now)
{
    gfx_clear();

    kangaroo_t k = {
        .cx = 64 + 2,
        .cy = 26,
        .scale = 1,
        .bounce = 0,
        .squash = 0,
        .tilt = 0,
        .blink = now < s_blink_until,
        .mouth = 0,
        .mood = MOOD_IDLE,
        .hold_box = true,
    };

    const char *subtitle = NULL;
    float t = (float)(now - s_scene_start) / 1000.0f;

    switch (s_scene) {
    case SCENE_BOOT:
        k.bounce = -(int)(4.0f * fabsf(sinf(t * 3.0f)));   /* 上蹦两下 */
        k.squash = (int)roundf(1.5f * sinf(t * 6.0f));
        k.mood = MOOD_IDLE;
        k.hold_box = true;
        subtitle = "单片机可真好玩";
        break;

    case SCENE_IDLE: {
        /* 呼吸 + 偶发眨眼，安静待机 */
        k.squash = (int)roundf(1.4f * sinf(t * 1.6f));
        k.mood = MOOD_IDLE;
        k.hold_box = true;
        subtitle = "PRESS BOOT";
        break;
    }

    case SCENE_LINE: {
        const line_t *l = &LINES[s_line_idx];
        k.mood = l->mood;
        k.hold_box = l->hold_box;
        subtitle = l->text;

        if (l->mood == MOOD_SMUG) {
            /* 嚣张：左右抖 + 眯眼 */
            k.tilt = ((now / 130) % 2) ? 1 : -1;
            k.squash = 1;
        } else if (l->mood == MOOD_ANGRY) {
            /* 生气：上下跳 */
            k.bounce = ((now / 110) % 2) ? -4 : 0;
        } else {
            k.squash = (int)roundf(1.0f * sinf(t * 3.0f));
        }

        /* 口型：蜂鸣器在响的时候张嘴，和节奏对上 */
        if (buzzer_busy()) {
            k.mouth = ((now / 85) % 2) ? 3 : 1;
        }
        break;
    }

    case SCENE_ALARM:
        /* 场景已预留：以后接上超声波/门磁之类，超阈值就 set_scene(SCENE_ALARM)
         * 并 buzzer_play(BUZZ_ALARM, 6) */
        k.mood = MOOD_ANGRY;
        k.tilt = ((now / 90) % 2) ? 2 : -2;
        k.bounce = ((now / 90) % 2) ? -3 : 0;
        subtitle = "敢抢我的外卖";
        break;
    }

    kangaroo_draw(&k);

    draw_divider();
    if (subtitle) {
        int w = gfx_measure_cn(subtitle);
        int x = marquee_x(w, now);
        gfx_text_cn(x, SUBTITLE_Y, subtitle, COL_WHITE, 1);
    }
    if (s_muted) {
        draw_mute_icon();
    }
}

/* ------------------------------------------------------------------ 按键 */

static bool s_btn_down;
static int64_t s_btn_press_at;
static bool s_btn_long_fired;
static bool s_pending_click;      /* 短按抬起后，等待是否还有第二击 */
static int64_t s_pending_click_at;
static bool s_skip_release;       /* 双击已消费，忽略随之而来的那次松手 */

#define LONG_PRESS_MS 1200
#define DOUBLE_CLICK_MS 320       /* 双击判定窗口 */

static void toggle_mute(void)
{
    s_muted = !s_muted;
    buzzer_mute(s_muted);
    ESP_LOGI(TAG, "buzzer %s", s_muted ? "muted" : "unmuted");
    if (!s_muted) {
        buzzer_play(&BUZZ_TICK, 1);   /* 解除静音时"嘀"一声确认 */
    }
}

static void start_line(int idx, int64_t now)
{
    if (idx < 0) {
        idx = 0;
    }
    s_line_idx = idx % LINE_COUNT;
    const line_t *l = &LINES[s_line_idx];
    buzzer_play(l->notes, l->notes_len);
    s_line_end = now + line_duration_ms(l);
    set_scene(SCENE_LINE, now);
    ESP_LOGI(TAG, "line %d: %s", s_line_idx, l->text);
}

/* 触发报警：进入报警场景并播放 BUZZ_ALARM（之前这块完全是预留桩，没人调用） */
static void trigger_alarm(int64_t now)
{
    set_scene(SCENE_ALARM, now);
    buzzer_play(BUZZ_ALARM, 6);
    ESP_LOGI(TAG, "ALARM triggered (double-click)");
}

static void poll_button(int64_t now)
{
    bool pressed = gpio_get_level((gpio_num_t)BTN_GPIO) == 0;   /* BOOT 键按下为低 */

    if (pressed && !s_btn_down) {
        s_btn_down = true;
        s_btn_press_at = now;
        s_btn_long_fired = false;
        /* 若上一次短按还在"等待第二击"，那这次按下就是双击 -> 报警 */
        if (s_pending_click) {
            s_pending_click = false;
            s_skip_release = true;     /* 这次抬起别再当一次短按 */
            trigger_alarm(now);
        }
        return;
    }

    if (pressed && s_btn_down) {
        /* 长按到点立刻生效，不用等松手 */
        if (!s_btn_long_fired && (now - s_btn_press_at) >= LONG_PRESS_MS) {
            s_btn_long_fired = true;
            s_pending_click = false;   /* 长按优先，取消待定的单击 */
            toggle_mute();
        }
        return;
    }

    if (!pressed && s_btn_down) {
        s_btn_down = false;
        if (s_skip_release) {          /* 双击已处理，吞掉这次松手 */
            s_skip_release = false;
            return;
        }
        if (!s_btn_long_fired && (now - s_btn_press_at) < LONG_PRESS_MS) {
            /* 一次短按抬起：先挂着，等 DOUBLE_CLICK_MS 看有没有第二击 */
            s_pending_click = true;
            s_pending_click_at = now;
        }
    }

    /* 单击超时未被接成双击 -> 当作"切台词"执行（延迟一点点，换取双击可用） */
    if (s_pending_click && (now - s_pending_click_at) >= DOUBLE_CLICK_MS) {
        s_pending_click = false;
        start_line(s_line_idx + 1, now);
    }
}

static void button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,      /* 板载 BOOT 已有上拉，再使能一次更稳 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
}

/* ------------------------------------------------------------------ 主流程 */

void app_main(void)
{
    ESP_LOGI(TAG, "袋鼠整活盒启动 (ESP32-S2)");

    ESP_ERROR_CHECK(buzzer_init(BZR_GPIO));
    button_init();

    esp_err_t err = ssd1306_init(OLED_SDA, OLED_SCL, OLED_ADDR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED 初始化失败: %s —— 屏幕不工作，其余功能继续", esp_err_to_name(err));
        /* 屏幕挂了也要能响，方便排查是接线问题还是别的问题 */
    } else {
        ssd1306_set_contrast(0xCF);
    }

    int64_t now = now_ms();
    set_scene(SCENE_BOOT, now);
    s_next_blink = now + 1500;
    s_next_auto = now + (int64_t)CONFIG_IDLE_AUTOSHOW_SEC * 1000;
    buzzer_play(BUZZ_BOOT, 3);

    while (1) {
        now = now_ms();

        /* 眨眼节拍：随机间隔，比固定周期自然 */
        if (now > s_next_blink) {
            s_blink_until = now + 130;
            s_next_blink = now + 1800 + (int)(esp_random() % 3200);
        }

        switch (s_scene) {
        case SCENE_BOOT:
            if (now - s_scene_start > 1600) {
                set_scene(SCENE_IDLE, now);
            }
            break;

        case SCENE_IDLE:
#if CONFIG_IDLE_AUTOSHOW_SEC > 0
            if (now > s_next_auto) {
                start_line((int)(esp_random() % LINE_COUNT), now);
            }
#endif
            break;

        case SCENE_LINE:
            if (now > s_line_end && !buzzer_busy()) {
                set_scene(SCENE_IDLE, now);
                s_next_auto = now + (int64_t)CONFIG_IDLE_AUTOSHOW_SEC * 1000;
            }
            break;

        case SCENE_ALARM:
            if (now - s_scene_start > 2000) {
                set_scene(SCENE_IDLE, now);
            }
            break;
        }

        poll_button(now);
        buzzer_tick();

        render(now);
        err = ssd1306_flush();
        if (err != ESP_OK) {
            /* 偶尔一次 I2C 失败不用慌，下一帧继续；连续失败再排查接线 */
            ESP_LOGW(TAG, "flush failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}

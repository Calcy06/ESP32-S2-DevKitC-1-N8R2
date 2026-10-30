/**
 * ESP32-S2 有源高电平触发蜂鸣器示例
 *
 * 功能：蜂鸣器每 3 秒鸣响 1 秒，循环执行（周期 3000ms，响 1000ms，停 2000ms）。
 *
 * 【本版本特点：最精简，工程里只有 5 个源文件】
 *   - 不含 Kconfig.projbuild：menuconfig 里没有蜂鸣器专属菜单。
 *     引脚 / 有效电平 / 周期 / 时长 / 实现方式全部在下面的 #define 里改，
 *     改完重新 `idf.py build` 即可。
 *   - 不含 sdkconfig.defaults：不预设任何编译期配置。
 *     芯片型号必须自己执行 `idf.py set-target esp32s2`（否则可能被编成 esp32）。
 *   - menuconfig 依然照常用，只是用来配置 ESP-IDF 自带选项
 *     （FreeRTOS 节拍、日志等级、Flash 大小、CPU 频率……），见 README 第 5 节。
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"   /* CONFIG_XXX 与 IDF 内部配置宏都来自这里 */

#include "buzzer.h"

static const char *TAG = "buzzer_demo";

/* ========================================================================== */
/* 蜂鸣器参数：想改引脚 / 周期 / 时长 / 实现方式，直接改这里                  */
/* ========================================================================== */
#define BUZZER_GPIO_NUM   GPIO_NUM_4   /* 控制引脚。GPIO4 是安全脚，见 README 第 4 节 */
#define BUZZER_ACTIVE_LVL 1            /* 1 = 高电平触发；0 = 低电平触发 */
#define BUZZER_PERIOD_MS  3000         /* 鸣响周期（ms） */
#define BUZZER_ON_MS      1000         /* 周期内鸣响时长（ms） */
#define BUZZER_OFF_MS     (BUZZER_PERIOD_MS - BUZZER_ON_MS)   /* 静默时长 */

/* 实现方式开关：0 = FreeRTOS 任务（推荐）；1 = FreeRTOS 软件定时器 */
#define BUZZER_USE_SW_TIMER 0

/* 编译期检查：鸣响时长不能超过周期，否则关断延时算出负数会导致死等 */
_Static_assert(BUZZER_ON_MS > 0, "BUZZER_ON_MS must be > 0");
_Static_assert(BUZZER_ON_MS < BUZZER_PERIOD_MS, "BUZZER_ON_MS must be < BUZZER_PERIOD_MS");

/* ========================================================================== */
/* 方式一：FreeRTOS 任务（BUZZER_USE_SW_TIMER = 0，默认）                      */
/* ========================================================================== */
#if !BUZZER_USE_SW_TIMER

#define BUZZER_TASK_STACK (2048)   /* 单位：字节。含日志输出时 2KB 够用 */
#define BUZZER_TASK_PRIO  (5)      /* 高于 IDLE(0) 即可 */

static void buzzer_task(void *arg)
{
    /* vTaskDelayUntil 比 vTaskDelay 更准：
       它是「相对上一次唤醒时刻」延时，不会把任务自身执行时间累积进周期，
       因此长期运行的 3 秒周期不会越走越慢。 */
    TickType_t last_wake_tick = xTaskGetTickCount();

    while (1) {
        /* ---- 1. 鸣响阶段 ---- */
        buzzer_on();
        ESP_LOGI(TAG, "BEEP ON  | GPIO%d = %d | %d ms",
                 (int)BUZZER_GPIO_NUM, BUZZER_ACTIVE_LVL, BUZZER_ON_MS);
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(BUZZER_ON_MS));

        /* ---- 2. 静默阶段 ---- */
        buzzer_off();
        ESP_LOGI(TAG, "BEEP OFF | GPIO%d = %d | %d ms",
                 (int)BUZZER_GPIO_NUM, !BUZZER_ACTIVE_LVL, BUZZER_OFF_MS);
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(BUZZER_OFF_MS));
    }

    /* 理论上不会走到这里；保险起见按规范删除自身 */
    vTaskDelete(NULL);
}

/* ========================================================================== */
/* 方式二：FreeRTOS 软件定时器回调（BUZZER_USE_SW_TIMER = 1）                  */
/* ========================================================================== */
#else

/* 定时器以 1 秒为节拍；一个周期切成 3 个时间片：第 0 片响，第 1、2 片不响。
   这样 3 秒周期 / 1 秒时长就变成了「按秒打拍子 + 计数取模」。 */
#define BUZZER_TIMER_TICK_MS (1000U)
#define BUZZER_SLOTS_TOTAL   (BUZZER_PERIOD_MS / BUZZER_TIMER_TICK_MS)
#define BUZZER_SLOTS_ON      (BUZZER_ON_MS / BUZZER_TIMER_TICK_MS)

/* 只有在周期和时长都是整秒时，这种「打拍子」写法才严格等价 */
_Static_assert(BUZZER_PERIOD_MS % BUZZER_TIMER_TICK_MS == 0,
               "软件定时器方案要求 BUZZER_PERIOD_MS 为 1000 的整数倍");
_Static_assert(BUZZER_ON_MS % BUZZER_TIMER_TICK_MS == 0,
               "软件定时器方案要求 BUZZER_ON_MS 为 1000 的整数倍");

static TimerHandle_t s_buzzer_timer = NULL;
static uint32_t      s_tick_count   = 0;

/**
 * 定时器回调函数。
 *
 * 【重要】这个回调运行在 FreeRTOS 的定时器守护任务（Timer Service task）里，
 * 属于软件定时器上下文，绝对不能做任何阻塞操作：
 *   禁止 vTaskDelay / 死等 / 大量串口打印 / 拿互斥量长时间等待。
 * 这里只做一次 gpio_set_level，非常轻量，符合要求。
 */
static void buzzer_timer_cb(TimerHandle_t xTimer)
{
    uint32_t idx = s_tick_count % BUZZER_SLOTS_TOTAL;   /* 0,1,2,0,1,2... */
    bool     on  = (idx < BUZZER_SLOTS_ON);             /* 每个周期第 0 片 = 响 */

    buzzer_set(on);
    ESP_LOGI(TAG, "timer tick %lu (slot %lu) -> %s",
             (unsigned long)s_tick_count, (unsigned long)idx, on ? "BEEP ON" : "BEEP OFF");

    s_tick_count++;
}

#endif

/* ========================================================================== */
/* 入口                                                                        */
/* ========================================================================== */
void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S2 active buzzer demo (period %d ms, on %d ms)",
             BUZZER_PERIOD_MS, BUZZER_ON_MS);

    /* 初始化蜂鸣器引脚；配置完成后默认处于「关闭」电平 */
    ESP_ERROR_CHECK(buzzer_init(BUZZER_GPIO_NUM, BUZZER_ACTIVE_LVL));

    /* 上电自检：短鸣 150ms，方便一眼确认「代码在跑 + 蜂鸣器接对了」。
       不需要的话删掉这三行即可。 */
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(150));
    buzzer_off();
    ESP_LOGI(TAG, "startup self-test beep done");

#if !BUZZER_USE_SW_TIMER

    BaseType_t ret = xTaskCreate(buzzer_task,      /* 任务函数 */
                                 "buzzer_task",    /* 任务名（调试用，≤16 字符） */
                                 BUZZER_TASK_STACK,/* 栈大小（字节） */
                                 NULL,             /* 传参 */
                                 BUZZER_TASK_PRIO, /* 优先级 */
                                 NULL);            /* 任务句柄（不需要则 NULL） */
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create buzzer task");
    }

#else

    /* pdTRUE = 自动重载（周期定时器）；最后一个参数为定时器 ID，这里不用 */
    s_buzzer_timer = xTimerCreate("buzzer_tmr",
                                  pdMS_TO_TICKS(BUZZER_TIMER_TICK_MS),
                                  pdTRUE,
                                  NULL,
                                  buzzer_timer_cb);
    if (s_buzzer_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create buzzer software timer");
        return;
    }
    /* 启动定时器；第二个参数是「等待命令队列可用的最大阻塞时间」，
       在任务上下文里调用，给 portMAX_DELAY 是安全的 */
    if (xTimerStart(s_buzzer_timer, portMAX_DELAY) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start buzzer software timer");
    }
    ESP_LOGI(TAG, "software timer started: tick=%dms, period=%d ticks",
             BUZZER_TIMER_TICK_MS, BUZZER_SLOTS_TOTAL);

#endif

    /* app_main 返回后任务/定时器仍在运行，主任务自动结束（IDF 会回收） */
}

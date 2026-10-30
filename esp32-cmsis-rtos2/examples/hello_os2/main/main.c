/*
 * ESP32 + CMSIS-RTOS2 综合示例
 *
 * 这份代码刻意写得与 STM32 上的 CMSIS-RTOS2 完全一致 ——
 * 你可以把它直接拷回去对比。唯一的区别只有两点：
 *   1. 入口函数是 app_main() 而不是 main()
 *   2. 多用了一个 esp_cmsis_os2_ext.h 里的 osThreadNewCore() 来指定跑在哪个核
 *
 * 演示内容：
 *   - 消息队列传递结构体
 *   - 互斥量保护共享数据
 *   - 线程标志做轻量通知
 *   - 事件组做多条件同步
 *   - 软件定时器周期回调
 *   - 内存池定长块分配
 *   - 把任务钉到 APP_CPU / PRO_CPU
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

#include "cmsis_os2.h"
#include "esp_cmsis_os2_ext.h"

#define LED_GPIO            GPIO_NUM_2   /* 多数 ESP32 开发板的板载 LED */
#define EVT_SENSOR_READY    (1U << 0)
#define EVT_NET_OK          (1U << 1)

/* ---------------------------- 共享对象 ---------------------------- */

typedef struct {
    uint32_t tick_ms;
    int16_t  value;
    char     unit[8];
} sensor_msg_t;

static osMessageQueueId_t   s_sensor_q;
static osMutexId_t          s_i2c_mtx;
static osEventFlagsId_t     s_events;
static osTimerId_t          s_timer;
static osMemoryPoolId_t     s_pool;
static osThreadId_t         s_producer_id;
static osThreadId_t         s_consumer_id;

static volatile int  s_total = 0;      /* 由 s_i2c_mtx 保护 */

/* ------------------------- 软件定时器回调 ------------------------- */

static void heartbeat_cb(void *arg)
{
    (void)arg;
    static uint32_t n = 0;
    printf("[timer ] heartbeat #%lu  (core %lu)\n",
           (unsigned long)(++n), (unsigned long)osThreadGetCoreId());
}

/* --------------------------- 生产者任务 --------------------------- */

static void sensor_producer(void *arg)
{
    sensor_msg_t msg;
    uint32_t     i = 0;

    (void)arg;
    printf("[prod  ] started on core %lu\n", (unsigned long)osThreadGetCoreId());

    for (;;) {
        /* 模拟读取传感器: 互斥起来访问 I2C 总线 */
        if (osMutexAcquire(s_i2c_mtx, 100) == osOK) {
            msg.value   = (int16_t)((i * 37) % 100);
            msg.tick_ms = osKernelGetTickCount();
            strcpy(msg.unit, "C");
            osMutexRelease(s_i2c_mtx);
        }

        /* 投递到队列: 消息是值拷贝, msg 出了作用域也无所谓 */
        if (osMessageQueuePut(s_sensor_q, &msg, 0, osWaitForever) == osOK) {
            osThreadFlagsSet(s_consumer_id, 0x01U);      /* 轻量通知一下 */
        }

        if (++i == 5U) {
            osEventFlagsSet(s_events, EVT_SENSOR_READY); /* 传感器就绪 */
        }

        osDelay(pdMS_TO_TICKS(500));   /* 注意: timeout 单位是 tick */
    }
}

/* --------------------------- 消费者任务 --------------------------- */

static void sensor_consumer(void *arg)
{
    sensor_msg_t msg;
    void        *blk;

    (void)arg;
    printf("[cons  ] started on core %lu\n", (unsigned long)osThreadGetCoreId());

    for (;;) {
        /* 阻塞等生产者通知(线程标志), 不必轮询 */
        uint32_t flags = osThreadFlagsWait(0x01U, osFlagsWaitAny, osWaitForever);
        if (flags & 0x80000000U) {
            continue;   /* 错误值, 跳过 */
        }

        while (osMessageQueueGet(s_sensor_q, &msg, NULL, 0) == osOK) {
            /* 内存池: 借用一块固定大小内存做临时缓冲区 */
            blk = osMemoryPoolAlloc(s_pool, 0);
            if (blk != NULL) {
                snprintf((char *)blk, 64, "%d%s", msg.value, msg.unit);
                printf("[cons  ] got value=%-8s at tick=%lu  pool_space=%lu\n",
                       (char *)blk, (unsigned long)msg.tick_ms,
                       (unsigned long)osMemoryPoolGetSpace(s_pool));
                osMemoryPoolFree(s_pool, blk);
            }

            osMutexAcquire(s_i2c_mtx, osWaitForever);
            s_total += msg.value;
            osMutexRelease(s_i2c_mtx);
        }
    }
}

/* --------------------------- LED 闪烁任务 --------------------------- */

static void led_task(void *arg)
{
    bool level = false;

    (void)arg;
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    /* 等待「传感器就绪」或「网络就绪」中的任意一个事件 */
    printf("[led   ] waiting for startup events...\n");
    uint32_t bits = osEventFlagsWait(s_events, EVT_SENSOR_READY | EVT_NET_OK,
                                     osFlagsWaitAny, osWaitForever);
    printf("[led   ] got event bits = 0x%02lX, blinking now (core %lu)\n",
           (unsigned long)bits, (unsigned long)osThreadGetCoreId());

    for (;;) {
        level = !level;
        gpio_set_level(LED_GPIO, level ? 1 : 0);
        osDelay(pdMS_TO_TICKS(300));
    }
}

/* ------------------------------ 入口 ------------------------------ */

/* ESP-IDF 的入口是 app_main()，此时调度器已经在运行。
 * STM32 上习惯的 osKernelInitialize() / osKernelStart() 三步曲
 * 在这里仍然是合法的 —— 适配层已把它们处理成幂等操作。 */
void app_main(void)
{
    osThreadAttr_t attr;

    osKernelInitialize();
    osKernelStart();

    /* 创建内核对象 */
    s_sensor_q = osMessageQueueNew(8, sizeof(sensor_msg_t), NULL);
    s_i2c_mtx  = osMutexNew(NULL);
    s_events   = osEventFlagsNew(NULL);
    s_pool     = osMemoryPoolNew(8, 64, NULL);

    /* 软件定时器: 每秒心跳一次 */
    s_timer = osTimerNew(heartbeat_cb, osTimerPeriodic, NULL, NULL);
    if (s_timer != NULL) {
        osTimerStart(s_timer, pdMS_TO_TICKS(1000));
    }

    /* 创建任务并按需钉核 */
    memset(&attr, 0, sizeof(attr));

    attr.name       = "producer";
    attr.priority   = osPriorityNormal;
    attr.stack_size = 4096;
    s_producer_id   = osThreadNewCore(sensor_producer, NULL, &attr, OS2_CORE_APP_CPU);

    attr.name       = "consumer";
    attr.priority   = osPriorityAboveNormal;
    s_consumer_id   = osThreadNewCore(sensor_consumer, NULL, &attr, OS2_CORE_APP_CPU);

    attr.name       = "led";
    attr.priority   = osPriorityBelowNormal;
    (void)osThreadNewCore(led_task, NULL, &attr, OS2_CORE_PRO_CPU);

    /* 演示多条件同步：手动触发「网络就绪」事件 */
    osEventFlagsSet(s_events, EVT_NET_OK);

    printf("[app   ] all objects and tasks created, running on core %lu\n",
           (unsigned long)osThreadGetCoreId());
}

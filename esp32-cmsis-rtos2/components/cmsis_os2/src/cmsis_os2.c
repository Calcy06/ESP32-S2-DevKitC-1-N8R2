/* --------------------------------------------------------------------------
 *  cmsis_os2.c  --  CMSIS-RTOS2 v2 在 FreeRTOS (ESP-IDF) 上的实现
 *
 *  相比 ARM 官方 CMSIS-FreeRTOS 版本，这里针对 ESP-IDF 做了以下关键改动：
 *
 *   1. osKernelStart() 不再调用 vTaskStartScheduler()
 *      ESP-IDF 在调用 app_main() 之前调度器已经启动，且 IDF 明确禁止
 *      用户调用 vTaskStartScheduler()/vTaskEndScheduler()。
 *
 *   2. IS_IRQ() 改用 xPortInIsrContext()
 *      官方实现读 ARM Cortex-M 的 IPSR 寄存器判断中断上下文，
 *      ESP32 是 Xtensa / RISC-V，没有 IPSR，改用 ESP-IDF 提供的 API。
 *
 *   3. 栈大小单位统一为「字节」
 *      ESP-IDF 修改了 usStackDepth 的语义：单位是字节而非 vanilla 的「字」。
 *      这恰好与 CMSIS-RTOS2 的 stack_size 单位一致。
 *
 *   4. osThreadNew() 内部走 PinnedToCore 版本
 *      保留 core affinity 能力，方便把任务钉到 PRO_CPU / APP_CPU。
 *
 *   5. osKernelLock/Unlock 在 SMP 上只影响当前核
 *      ESP-IDF FreeRTOS 的 vTaskSuspendAll() 只挂起调用核的调度器,
 *      因此它不能用于跨核互斥 —— 详见 README 的警告。
 *
 *  License: Apache-2.0 (derived from ARM-software/CMSIS-FreeRTOS)
 * -------------------------------------------------------------------------- */

#include "cmsis_os2.h"
#include "cmsis_os2_priv.h"

#include <string.h>
#include <stdlib.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

#include "esp_cmsis_os2_ext.h"
/* ESP-IDF 提供的高精度计时器 (微秒级)，用于 osKernelGetSysTimerCount */
#include "esp_timer.h"
#else
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "event_groups.h"
#endif

/* ==========================================================================
 *  平台抽象层
 * ========================================================================== */

/* --- 中断上下文判别 ------------------------------------------------------ */
#ifdef ESP_PLATFORM
/* ESP-IDF 提供；声明在此以便不依赖具体头路径 */
extern BaseType_t xPortInIsrContext(void);
#define IS_IRQ() (xPortInIsrContext() != pdFALSE)
#else
/* 非 ESP 平台（如本机 POSIX 模拟）回退为「永远不在中断中」 */
#define IS_IRQ() (0)
#endif

/* --- 栈单位换算 ---------------------------------------------------------- */
#ifdef ESP_PLATFORM
/* ESP-IDF FreeRTOS: usStackDepth 与 uxTaskGetStackHighWaterMark 均为字节 */
#define OS2_STACK_TO_RTOS(bytes) ((UBaseType_t)(bytes))
#define OS2_STACK_FROM_RTOS(raw) ((uint32_t)(raw))
#else
/* vanilla FreeRTOS: 单位为 StackType_t 字 */
#define OS2_STACK_TO_RTOS(bytes) ((UBaseType_t)((bytes) / sizeof(StackType_t)))
#define OS2_STACK_FROM_RTOS(raw) ((uint32_t)((uint32_t)(raw) * sizeof(StackType_t)))
#endif

/* --- tick 换算------------------------------------------------------------ */
#define OS2_TICK_FOREVER 0xFFFFFFFFU

/* 把 CMSIS 的 timeout(tick) 转成 FreeRTOS 的 xTicksToWait */
static inline TickType_t os2_timeout_to_ticks(uint32_t timeout)
{
    if (timeout == OS2_TICK_FOREVER)
    {
        return portMAX_DELAY;
    }
    if (timeout == 0U)
    {
        return (TickType_t)0;
    }
    return (TickType_t)timeout;
}

/* --- 临界区抽象 -----------------------------------------------------------
 *  ESP-IDF 是 SMP：taskENTER_CRITICAL()/taskEXIT_CRITICAL() 需要传入一个
 *  portMUX_TYPE 自旋锁参数（禁用中断在 SMP 上不构成互斥）；而 vanilla
 *  FreeRTOS 的版本无需参数。这里统一封装，保证两套内核都能编译。
 *
 *  任务上下文： OS2_CRITICAL_ENTER()/OS2_CRITICAL_EXIT()
 *  中断上下文： OS2_ISR_CRITICAL_ENTER(saved)/OS2_ISR_CRITICAL_EXIT(saved)
 * -------------------------------------------------------------------------- */
#ifdef ESP_PLATFORM
static portMUX_TYPE s_os2_cs_mux = portMUX_INITIALIZER_UNLOCKED;

#define OS2_CRITICAL_ENTER() taskENTER_CRITICAL(&s_os2_cs_mux)
#define OS2_CRITICAL_EXIT() taskEXIT_CRITICAL(&s_os2_cs_mux)

#define OS2_ISR_CRITICAL_ENTER(saved)          \
    do                                         \
    {                                          \
        (void)(saved);                         \
        taskENTER_CRITICAL_ISR(&s_os2_cs_mux); \
    } while (0)
#define OS2_ISR_CRITICAL_EXIT(saved)          \
    do                                        \
    {                                         \
        (void)(saved);                        \
        taskEXIT_CRITICAL_ISR(&s_os2_cs_mux); \
    } while (0)
#else
#define OS2_CRITICAL_ENTER() taskENTER_CRITICAL()
#define OS2_CRITICAL_EXIT() taskEXIT_CRITICAL()

#define OS2_ISR_CRITICAL_ENTER(saved)            \
    do                                           \
    {                                            \
        (saved) = taskENTER_CRITICAL_FROM_ISR(); \
    } while (0)
#define OS2_ISR_CRITICAL_EXIT(saved)       \
    do                                     \
    {                                      \
        taskEXIT_CRITICAL_FROM_ISR(saved); \
    } while (0)
#endif

/* --- 任务通知索引 --------------------------------------------------------- */
#ifndef tskDEFAULT_INDEX_TO_NOTIFY
#define tskDEFAULT_INDEX_TO_NOTIFY 0U
#endif

/* ==========================================================================
 *  优先级双向映射
 *
 *  CMSIS:  osPriorityIdle(1) .. osPriorityISR(56)   -- 数值越大优先级越高
 *  RTOS :  0 .. (configMAX_PRIORITIES - 1)          -- 数值越大优先级越高
 *
 *  CMSIS 有 56 级，而 ESP-IDF 默认 configMAX_PRIORITIES=25（RTOS 可用 [0,24]），
 *  映射必然有损。这里采用「锚点精确往返」策略：
 *    - 8 个主档（Idle / Low / BelowNormal / Normal / AboveNormal / High /
 *      Realtime / ISR，间隔 8）被精确地锚定到 RTOS 优先级上，set 后再 get
 *      能精确得到原来的主档值；
 *    - 主档之间的细分值（如 Normal1..Normal7）线性插值到相邻锚点之间，
 *      若空隙不够则会被压缩到相同优先级（有损但单调）。
 *
 *  刻意避开 RTOS 优先级 0（ESP-IDF 由 IDLE 任务占用）。
 * ========================================================================== */

#define OS2_PRIO_MIN 1
#define OS2_PRIO_MAX 56
#define OS2_PRIO_LEVELS 7 /* 7 个区间分隔出 8 个锚点 */

/* 第 level 个锚点（level: 0..7 对应 Idle..ISR）对应的 RTOS 优先级 */
static UBaseType_t os2_anchor(int32_t level)
{
    UBaseType_t maxp = (UBaseType_t)(configMAX_PRIORITIES - 1);

    if (maxp <= 1U)
    {
        return (UBaseType_t)1U;
    }
    /* 把 [1, maxp] 均分成 OS2_PRIO_LEVELS 份，四舍五入取第 level 个分割点 */
    return (UBaseType_t)(1U + (uint32_t)(((uint32_t)level * (maxp - 1U) * 2U + (uint32_t)OS2_PRIO_LEVELS) / (2U * (uint32_t)OS2_PRIO_LEVELS)));
}

static UBaseType_t os2_prio_to_freertos(osPriority_t prio)
{
    int32_t p = (int32_t)prio;
    UBaseType_t maxp = (UBaseType_t)(configMAX_PRIORITIES - 1);
    int32_t idx, frac;
    UBaseType_t base, next, gap;

    if (p <= (int32_t)osPriorityNone)
    {
        p = OS2_PRIO_MIN; /* 1 */
    }
    if (p >= OS2_PRIO_MAX)
    {
        return maxp; /* ISR 及以上 -> 最高 */
    }
    if (maxp <= 1U)
    {
        return (UBaseType_t)1U;
    }

    /* 分解出主档 idx 与档内偏移 frac */
    if (p >= 8)
    {
        idx = 1 + (p - 8) / 8; /* Low(8) 起每 8 一档 */
        frac = (p - 8) % 8;
    }
    else
    {
        idx = 0;      /* Idle 段：p=1..7 */
        frac = p - 1; /* 0..6 */
    }

    base = os2_anchor(idx);
    next = os2_anchor(idx + 1);
    if (next <= base)
    {
        return base;
    }
    gap = next - base;
    /* frac=0 -> base（锚点精确）；frac 越大越接近 next-1，保证不越界 */
    return base + (UBaseType_t)(((uint32_t)frac * (gap - 1U)) / 8U);
}

static osPriority_t os2_prio_from_freertos(UBaseType_t fprio)
{
    UBaseType_t maxp = (UBaseType_t)(configMAX_PRIORITIES - 1);
    int32_t idx, frac;
    UBaseType_t base, next, gap;
    uint32_t f;

    if (maxp <= 1U)
    {
        return osPriorityIdle;
    }
    if (fprio > maxp)
    {
        fprio = maxp;
    }
    if (fprio == 0U)
    {
        fprio = 1U; /* 优先级 0 归 Idle */
    }

    /* 找 idx 使 anchor(idx) <= fprio < anchor(idx+1) */
    idx = 0;
    for (int32_t i = 0; i < OS2_PRIO_LEVELS; i++)
    {
        if (fprio < os2_anchor(i + 1))
        {
            break;
        }
        idx = i + 1;
    }

    base = os2_anchor(idx);
    next = os2_anchor(idx + 1);
    if (next <= base)
    {
        frac = 0;
    }
    else
    {
        gap = next - base;
        f = (uint32_t)(fprio - base);
        frac = (int32_t)((f * 8U + (gap - 1U) / 2U) / (gap - 1U));
    }

    if (idx == 0)
    {
        return (osPriority_t)(1 + frac);
    }
    return (osPriority_t)(8 * idx + frac);
}

/* ==========================================================================
 *  线程资源注册表（受临界区保护的单链表）
 *
 *  ESP32 是 SMP：临界区实现为自旋锁 + 本核中断屏蔽，
 *  足以保护这条极短的链表操作。
 * ========================================================================== */

static os2_thread_cb_t *s_thread_reg = NULL;

os2_thread_cb_t *os2_thread_reg_alloc(void)
{
    os2_thread_cb_t *cb = (os2_thread_cb_t *)pvPortMalloc(sizeof(os2_thread_cb_t));

    if (cb == NULL)
    {
        return NULL;
    }
    memset(cb, 0, sizeof(os2_thread_cb_t));

    OS2_CRITICAL_ENTER();
    {
        cb->next = s_thread_reg;
        s_thread_reg = cb;
    }
    OS2_CRITICAL_EXIT();

    return cb;
}

os2_thread_cb_t *os2_thread_reg_find(TaskHandle_t task)
{
    os2_thread_cb_t *cur;

    OS2_CRITICAL_ENTER();
    {
        for (cur = s_thread_reg; cur != NULL; cur = cur->next)
        {
            if (cur->task == task)
            {
                break;
            }
        }
    }
    OS2_CRITICAL_EXIT();

    return cur;
}

void os2_thread_reg_remove(TaskHandle_t task)
{
    os2_thread_cb_t *cur;
    os2_thread_cb_t *prev = NULL;

    OS2_CRITICAL_ENTER();
    {
        for (cur = s_thread_reg; cur != NULL; cur = cur->next)
        {
            if (cur->task == task)
            {
                if (prev == NULL)
                {
                    s_thread_reg = cur->next;
                }
                else
                {
                    prev->next = cur->next;
                }
                break;
            }
            prev = cur;
        }
    }
    OS2_CRITICAL_EXIT();

    if (cur != NULL)
    {
        vPortFree(cur);
    }
}

/* ==========================================================================
 *  内核控制
 * ========================================================================== */

osStatus_t osKernelInitialize(void)
{
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    /* ESP-IDF 的调度器在 app_main 之前就已初始化，无需任何动作 */
    return osOK;
}

osStatus_t osKernelGetInfo(osVersion_t *version, char *id_buf, uint32_t id_size)
{
    uint32_t size;

    if (version != NULL)
    {
        version->api = osVersionAPI;
        version->kernel = osVersionKernel;
    }
    if ((id_buf != NULL) && (id_size != 0U))
    {
        size = (uint32_t)strlen(osKernelSystemId);
        if (size >= id_size)
        {
            size = id_size - 1U;
        }
        memcpy(id_buf, osKernelSystemId, size);
        id_buf[size] = '\0';
    }
    return osOK;
}

osKernelState_t osKernelGetState(void)
{
    /* ESP-IDF 上调度器在 app_main 时已经运行 */
    return osKernelRunning;
}

osStatus_t osKernelStart(void)
{
    if (IS_IRQ())
    {
        return osErrorISR;
    }
#if defined(ESP_PLATFORM)
    /* ESP-IDF 禁止调用 vTaskStartScheduler()：调度器早已运行。
     * 因此 osKernelStart() 退化为幂等的状态确认。 */
    (void)0;
#else
    /* 非 ESP 平台（本机 POSIX 验证）：只有在调度器尚未启动时才启动。
     * 加 once-guard 避免重复启动挂死。 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
    {
        vTaskStartScheduler();
    }
#endif
    return osOK;
}

int32_t osKernelLock(void)
{
    int32_t lock;

    if (IS_IRQ())
    {
        return (int32_t)osErrorISR;
    }
    /* 注意：SMP 上 vTaskSuspendAll() 只挂起当前核，见 README 警告 */
    vTaskSuspendAll();
    lock = (int32_t)(xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ? 1 : 0);
    return lock + 1;
}

int32_t osKernelUnlock(void)
{
    int32_t lock;

    if (IS_IRQ())
    {
        return (int32_t)osErrorISR;
    }
    lock = (int32_t)(xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ? 1 : 0);
    (void)xTaskResumeAll();
    return lock;
}

int32_t osKernelRestoreLock(int32_t lock)
{
    if (IS_IRQ())
    {
        return (int32_t)osErrorISR;
    }
    if (lock <= 0)
    {
        return (int32_t)osErrorParameter;
    }
    /* 简化实现：restore 即 unlock */
    (void)xTaskResumeAll();
    return (int32_t)osOK;
}

uint32_t osKernelSuspend(void)
{
    uint32_t ticks;

    if (IS_IRQ())
    {
        return 0U;
    }
    ticks = (uint32_t)xTaskGetTickCount();
    vTaskSuspendAll();
    return ticks;
}

void osKernelResume(uint32_t sleep_ticks)
{
    (void)sleep_ticks;
    if (!IS_IRQ())
    {
        (void)xTaskResumeAll();
    }
}

uint32_t osKernelGetTickCount(void)
{
    TickType_t t;

    if (IS_IRQ())
    {
        t = xTaskGetTickCountFromISR();
    }
    else
    {
        t = xTaskGetTickCount();
    }
    return (uint32_t)t;
}

uint32_t osKernelGetTickFreq(void)
{
    return (uint32_t)configTICK_RATE_HZ;
}

uint32_t osKernelGetSysTimerCount(void)
{
#if defined(ESP_PLATFORM)
    /* ESP-IDF 提供微秒级单调计数，比 tick 精度高得多 */
    return (uint32_t)(esp_timer_get_time() & 0xFFFFFFFFU);
#elif defined(configCPU_CLOCK_HZ)
    return (uint32_t)(((uint64_t)xTaskGetTickCount() * (uint64_t)configCPU_CLOCK_HZ) /
                      (uint64_t)configTICK_RATE_HZ);
#else
    return (uint32_t)xTaskGetTickCount();
#endif
}

uint32_t osKernelGetSysTimerFreq(void)
{
#if defined(ESP_PLATFORM)
    return 1000000U; /* esp_timer 单位: Hz (微秒) */
#elif defined(configCPU_CLOCK_HZ)
    return (uint32_t)configCPU_CLOCK_HZ;
#else
    return (uint32_t)configTICK_RATE_HZ;
#endif
}

/* ==========================================================================
 *  线程管理
 * ========================================================================== */

osThreadId_t osThreadNew(osThreadFunc_t func, void *argument, const osThreadAttr_t *attr)
{
#if defined(ESP_PLATFORM)
    /* 单核芯片（S2/C3/C6/H2 等）忽略亲和性，传 NO_AFFINITY 是安全的 */
    return osThreadNewCore(func, argument, attr, OS2_CORE_NO_AFFINITY);
#else
    return osThreadNewCore(func, argument, attr, 0);
#endif
}

osThreadId_t osThreadNewCore(osThreadFunc_t func, void *argument,
                             const osThreadAttr_t *attr, BaseType_t xCoreID)
{
    TaskHandle_t task;
    os2_thread_cb_t *cb;
    UBaseType_t stack_depth;
    UBaseType_t prio;
    const char *name;
    uint32_t stack_size;

    if (func == NULL)
    {
        return NULL;
    }
    if (IS_IRQ())
    {
        return NULL;
    }
    if ((attr != NULL) && ((attr->attr_bits & osThreadJoinable) != 0U))
    {
        /* FreeRTOS 没有 join 语义 —— ARM 官方 CMSIS-FreeRTOS 同样不支持 */
        return NULL;
    }

    name = ((attr != NULL) && (attr->name != NULL)) ? attr->name : "os2_thread";
    stack_size = ((attr != NULL) && (attr->stack_size != 0U)) ? attr->stack_size
                                                              : (uint32_t)OS2_DEFAULT_STACK_BYTES;
    prio = os2_prio_to_freertos(((attr != NULL) && (attr->priority != osPriorityNone))
                                    ? attr->priority
                                    : osPriorityNormal);

    stack_depth = OS2_STACK_TO_RTOS(stack_size);
    if (stack_depth == 0U)
    {
        return NULL;
    }

#if defined(ESP_PLATFORM)
    /* 核编号越界保护：单核芯片（S2/C3/C6/H2）上传核 1 会让
     * xTaskCreatePinnedToCore 触发断言，这里提前拦下返回 NULL。 */
    if (xCoreID != OS2_CORE_NO_AFFINITY)
    {
        if ((xCoreID < 0) || (xCoreID >= portNUM_PROCESSORS))
        {
            return NULL;
        }
    }
#endif

    /* --- 创建 ---
     *
     * 策略（关键设计）：
     *   静态路径 —— 仅当 attr 同时提供了 cb_mem 和 stack_mem 时才走
     *                xTaskCreateStaticPinnedToCore()。此时 TCB/栈的所有权
     *                完全属于调用方，本层不做释放（FreeRTOS 也不会去释放
     *                静态创建的任务内存）。
     *   动态路径 —— 其它情况一律走 xTaskCreatePinnedToCore()，由 FreeRTOS
     *                堆托管 TCB/栈。任务被 vTaskDelete 后 Idle 任务自动回收，
     *                不会泄漏。
     *
     * 之所以不总是「malloc + xTaskCreateStatic」：静态创建的任务被删除时
     * FreeRTOS 不会回收其 TCB/栈，本层又无法知晓 Idle 何时完成回收，
     * 手工释放会造成释放后使用或内存泄漏。
     */
    task = NULL;
    if ((attr != NULL) && (attr->cb_mem != NULL) && (attr->cb_size >= sizeof(StaticTask_t)) &&
        (attr->stack_mem != NULL) && (attr->stack_size != 0U))
    {
        /* ---- 静态路径 ---- */
#if defined(ESP_PLATFORM)
        task = xTaskCreateStaticPinnedToCore(func, name, stack_depth, argument, prio,
                                             (StackType_t *)attr->stack_mem,
                                             (StaticTask_t *)attr->cb_mem, xCoreID);
#else
        task = xTaskCreateStatic(func, name, stack_depth, argument, prio,
                                 (StackType_t *)attr->stack_mem,
                                 (StaticTask_t *)attr->cb_mem);
#endif
    }
    else
    {
        /* ---- 动态路径 ---- */
#if defined(ESP_PLATFORM)
        (void)xTaskCreatePinnedToCore(func, name, stack_depth, argument, prio, &task, xCoreID);
#else
        (void)xTaskCreate(func, name, stack_depth, argument, prio, &task);
#endif
    }

    if (task == NULL)
    {
        return NULL;
    }

    cb = os2_thread_reg_alloc();
    if (cb == NULL)
    {
        /* 注册失败：任务已经成功创建，保守起见不删除它，
         * 仅导致 osThreadGetStackSize 无法返回精确值。 */
        return (osThreadId_t)task;
    }
    cb->task = task;
    cb->stack_size = stack_size;

    return (osThreadId_t)task;
}

const char *osThreadGetName(osThreadId_t thread_id)
{
    if (thread_id == NULL)
    {
        thread_id = (osThreadId_t)xTaskGetCurrentTaskHandle();
    }
    return pcTaskGetName((TaskHandle_t)thread_id);
}

osThreadId_t osThreadGetId(void)
{
    return (osThreadId_t)xTaskGetCurrentTaskHandle();
}

osThreadState_t osThreadGetState(osThreadId_t thread_id)
{
    eTaskState st;

    if (thread_id == NULL)
    {
        thread_id = (osThreadId_t)xTaskGetCurrentTaskHandle();
    }
#if (INCLUDE_eTaskGetState == 1)
    st = eTaskGetState((TaskHandle_t)thread_id);
#else
    (void)thread_id;
    return osThreadReady;
#endif

    switch (st)
    {
    case eRunning:
        return osThreadRunning;
    case eReady:
        return osThreadReady;
    case eBlocked:
    case eSuspended:
        return osThreadBlocked;
    case eDeleted:
        return osThreadTerminated;
    case eInvalid:
    default:
        return osThreadError;
    }
}

osStatus_t osThreadSetPriority(osThreadId_t thread_id, osPriority_t priority)
{
    UBaseType_t fprio = os2_prio_to_freertos(priority);

    if (IS_IRQ())
    {
        return osErrorISR;
    }
    if (thread_id == NULL)
    {
        thread_id = (osThreadId_t)xTaskGetCurrentTaskHandle();
    }
    vTaskPrioritySet((TaskHandle_t)thread_id, fprio);
    return osOK;
}

osPriority_t osThreadGetPriority(osThreadId_t thread_id)
{
    if (IS_IRQ())
    {
        return osPriorityError;
    }
    if (thread_id == NULL)
    {
        thread_id = (osThreadId_t)xTaskGetCurrentTaskHandle();
    }
    return os2_prio_from_freertos(uxTaskPriorityGet((TaskHandle_t)thread_id));
}

osStatus_t osThreadYield(void)
{
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    taskYIELD();
    return osOK;
}

osStatus_t osThreadSuspend(osThreadId_t thread_id)
{
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    if (thread_id == NULL)
    {
        thread_id = (osThreadId_t)xTaskGetCurrentTaskHandle();
    }
    vTaskSuspend((TaskHandle_t)thread_id);
    return osOK;
}

osStatus_t osThreadResume(osThreadId_t thread_id)
{
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    if (thread_id == NULL)
    {
        return osErrorParameter;
    }
    vTaskResume((TaskHandle_t)thread_id);
    return osOK;
}

osStatus_t osThreadDetach(osThreadId_t thread_id)
{
    (void)thread_id;
    /* FreeRTOS 任务默认即为 detached，无 join 语义 */
    return osOK;
}

osStatus_t osThreadJoin(osThreadId_t thread_id)
{
    (void)thread_id;
    /* FreeRTOS 不支持 join —— ARM 官方 CMSIS-FreeRTOS 同样返回错误 */
    return osError;
}

void osThreadExit(void)
{
    TaskHandle_t self;

    if (IS_IRQ())
    {
        return;
    }
    self = xTaskGetCurrentTaskHandle();
    os2_thread_reg_remove(self);
    vTaskDelete(NULL);
}

osStatus_t osThreadTerminate(osThreadId_t thread_id)
{
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    if (thread_id == NULL)
    {
        return osErrorParameter;
    }
    os2_thread_reg_remove((TaskHandle_t)thread_id);
    vTaskDelete((TaskHandle_t)thread_id);
    return osOK;
}

uint32_t osThreadGetStackSize(osThreadId_t thread_id)
{
    os2_thread_cb_t *cb;

    if (thread_id == NULL)
    {
        thread_id = (osThreadId_t)xTaskGetCurrentTaskHandle();
    }
    cb = os2_thread_reg_find((TaskHandle_t)thread_id);
    if (cb != NULL)
    {
        return cb->stack_size;
    }
    /* 非本层创建的任务（如 app_main）无法得知精确栈大小，返回默认值 */
    return OS2_DEFAULT_STACK_BYTES;
}

uint32_t osThreadGetStackSpace(osThreadId_t thread_id)
{
    if (thread_id == NULL)
    {
        thread_id = (osThreadId_t)xTaskGetCurrentTaskHandle();
    }
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    return OS2_STACK_FROM_RTOS(uxTaskGetStackHighWaterMark((TaskHandle_t)thread_id));
#else
    (void)thread_id;
    return 0U;
#endif
}

uint32_t osThreadGetCount(void)
{
#if (configUSE_TRACE_FACILITY == 1)
    return (uint32_t)uxTaskGetNumberOfTasks();
#else
    return 0U;
#endif
}

uint32_t osThreadEnumerate(osThreadId_t *thread_array, uint32_t array_items)
{
#if (configUSE_TRACE_FACILITY == 1)
    uint32_t n;
    TaskStatus_t *pxTaskStatusArray;
    uint32_t ulArraySize;
    uint32_t ulTaskCount;

    if ((thread_array == NULL) || (array_items == 0U))
    {
        return 0U;
    }

    ulTaskCount = (uint32_t)uxTaskGetNumberOfTasks();
    ulArraySize = (ulTaskCount > array_items) ? array_items : ulTaskCount;
    if (ulArraySize == 0U)
    {
        return 0U;
    }

    pxTaskStatusArray = (TaskStatus_t *)pvPortMalloc(ulTaskCount * sizeof(TaskStatus_t));
    if (pxTaskStatusArray == NULL)
    {
        return 0U;
    }

    ulTaskCount = uxTaskGetSystemState(pxTaskStatusArray, ulTaskCount, NULL);
    for (n = 0U; n < ulArraySize; n++)
    {
        thread_array[n] = (osThreadId_t)pxTaskStatusArray[n].xHandle;
    }
    vPortFree(pxTaskStatusArray);

    return ulArraySize;
#else
    (void)thread_array;
    (void)array_items;
    return 0U;
#endif
}

/* ==========================================================================
 *  线程标志（基于 FreeRTOS 任务通知）
 * ========================================================================== */

uint32_t osThreadFlagsSet(osThreadId_t thread_id, uint32_t flags)
{
    TaskHandle_t task = (TaskHandle_t)thread_id;
    uint32_t prev = 0U;

    if (task == NULL)
    {
        task = xTaskGetCurrentTaskHandle();
    }

    if (IS_IRQ())
    {
        BaseType_t yield = pdFALSE;
        (void)xTaskGenericNotifyFromISR(task, tskDEFAULT_INDEX_TO_NOTIFY,
                                        flags, eSetBits, &prev, &yield);
        portYIELD_FROM_ISR(yield);
    }
    else
    {
        (void)xTaskGenericNotify(task, tskDEFAULT_INDEX_TO_NOTIFY,
                                 flags, eSetBits, &prev);
    }
    return prev;
}

uint32_t osThreadFlagsClear(uint32_t flags)
{
    return ulTaskGenericNotifyValueClear(xTaskGetCurrentTaskHandle(),
                                         tskDEFAULT_INDEX_TO_NOTIFY, flags);
}

uint32_t osThreadFlagsGet(void)
{
    uint32_t val = 0U;

    /* xTaskGenericNotify 的第 5 个参数回传旧值，动作 eNoAction 不改变任何位 */
    (void)xTaskGenericNotify(xTaskGetCurrentTaskHandle(), tskDEFAULT_INDEX_TO_NOTIFY,
                             0U, eNoAction, &val);
    return val;
}

uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout)
{
    TaskHandle_t self;
    BaseType_t wait_all;
    BaseType_t no_clear;
    TickType_t ticks;
    uint32_t rflags;

    if (IS_IRQ())
    {
        return (uint32_t)osError;
    }

    self = xTaskGetCurrentTaskHandle();
    wait_all = (options & osFlagsWaitAll) ? pdTRUE : pdFALSE;
    no_clear = (options & osFlagsNoClear) ? pdTRUE : pdFALSE;
    ticks = os2_timeout_to_ticks(timeout);

    if (wait_all == pdFALSE)
    {
        /* 等待任意一个位：唤醒即返回，按需在退出时清除 */
        rflags = 0U;
        if (xTaskGenericNotifyWait(tskDEFAULT_INDEX_TO_NOTIFY,
                                   0U,
                                   (no_clear != pdFALSE) ? 0U : flags,
                                   &rflags, ticks) != pdPASS)
        {
            return (uint32_t)osErrorTimeout;
        }
        return rflags;
    }

    /* 等待全部位：循环累积，不丢失已到位的部分位。
     * 每次进入等待前清除「已满足」的位，从而只为缺失位阻塞，避免忙等。 */
    {
        TickType_t start = xTaskGetTickCount();
        uint32_t have = 0U;

        (void)xTaskGenericNotify(self, tskDEFAULT_INDEX_TO_NOTIFY,
                                 0U, eNoAction, &have);

        for (;;)
        {
            uint32_t missing = flags & ~have;

            if (missing == 0U)
            {
                break;
            }

            TickType_t remain;
            uint32_t got = 0U;

            if (ticks == portMAX_DELAY)
            {
                remain = portMAX_DELAY;
            }
            else
            {
                TickType_t elapsed = xTaskGetTickCount() - start;
                remain = (elapsed >= ticks) ? (TickType_t)0
                                            : (TickType_t)(ticks - elapsed);
            }

            /* 清除已满足的位后仅等待缺失位，已满足位保存在 have 中不丢 */
            if (xTaskGenericNotifyWait(tskDEFAULT_INDEX_TO_NOTIFY,
                                       have & flags, 0U, &got, remain) != pdPASS)
            {
                return (uint32_t)osErrorTimeout;
            }
            have |= got;
        }

        if (no_clear == pdFALSE)
        {
            (void)ulTaskGenericNotifyValueClear(self, tskDEFAULT_INDEX_TO_NOTIFY,
                                                flags);
        }
        return have;
    }
}

/* ==========================================================================
 *  通用等待（延迟）
 * ========================================================================== */

osStatus_t osDelay(uint32_t ticks)
{
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    vTaskDelay(os2_timeout_to_ticks(ticks));
    return osOK;
}

osStatus_t osDelayUntil(uint32_t ticks)
{
    TickType_t now;

    if (IS_IRQ())
    {
        return osErrorISR;
    }
    /* CMSIS 语义：等待直到系统 tick 达到绝对时刻 ticks。
     * 若已过则立即返回。 */
    now = xTaskGetTickCount();
    if ((TickType_t)ticks > now)
    {
        vTaskDelay((TickType_t)ticks - now);
    }
    return osOK;
}

/* ==========================================================================
 *  软件定时器
 * ========================================================================== */

typedef struct os2_timer_cb
{
    osTimerFunc_t func;
    void *arg;
} os2_timer_cb_t;

static void os2_timer_wrapper(TimerHandle_t hTimer)
{
    os2_timer_cb_t *cb = (os2_timer_cb_t *)pvTimerGetTimerID(hTimer);

    if ((cb != NULL) && (cb->func != NULL))
    {
        cb->func(cb->arg);
    }
}

osTimerId_t osTimerNew(osTimerFunc_t func, osTimerType_t type,
                       void *argument, const osTimerAttr_t *attr)
{
    TimerHandle_t hTimer;
    os2_timer_cb_t *cb;
    const char *name;
    UBaseType_t reload;

    if ((func == NULL) || IS_IRQ())
    {
        return NULL;
    }

    name = ((attr != NULL) && (attr->name != NULL)) ? attr->name : "os2_timer";
    reload = (type == osTimerPeriodic) ? (UBaseType_t)pdTRUE : (UBaseType_t)pdFALSE;

    cb = (os2_timer_cb_t *)pvPortMalloc(sizeof(os2_timer_cb_t));
    if (cb == NULL)
    {
        return NULL;
    }
    cb->func = func;
    cb->arg = argument;

    /* 用 FreeRTOS 的 timer ID 存放回调包装结构，wrapper 里取回 */
    hTimer = xTimerCreate(name, (TickType_t)1, reload, (void *)cb, os2_timer_wrapper);
    if (hTimer == NULL)
    {
        vPortFree(cb);
        return NULL;
    }

    return (osTimerId_t)hTimer;
}

const char *osTimerGetName(osTimerId_t timer_id)
{
    if (timer_id == NULL)
    {
        return NULL;
    }
    return pcTimerGetName((TimerHandle_t)timer_id);
}

osStatus_t osTimerStart(osTimerId_t timer_id, uint32_t ticks)
{
    TimerHandle_t hTimer = (TimerHandle_t)timer_id;
    TickType_t wait = os2_timeout_to_ticks(ticks);

    if (hTimer == NULL)
    {
        return osErrorParameter;
    }
    /* xTimerChangePeriod 会同时启动定时器（若未运行），符合 osTimerStart 语义 */
    if (IS_IRQ())
    {
        BaseType_t yield = pdFALSE;
        (void)xTimerChangePeriodFromISR(hTimer, wait, &yield);
        portYIELD_FROM_ISR(yield);
    }
    else
    {
        (void)xTimerChangePeriod(hTimer, wait, portMAX_DELAY);
    }
    return osOK;
}

osStatus_t osTimerStop(osTimerId_t timer_id)
{
    TimerHandle_t hTimer = (TimerHandle_t)timer_id;

    if (hTimer == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        BaseType_t yield = pdFALSE;
        (void)xTimerStopFromISR(hTimer, &yield);
        portYIELD_FROM_ISR(yield);
    }
    else
    {
        (void)xTimerStop(hTimer, portMAX_DELAY);
    }
    return osOK;
}

uint32_t osTimerIsRunning(osTimerId_t timer_id)
{
    if (timer_id == NULL)
    {
        return osTimerError;
    }
    if (xTimerIsTimerActive((TimerHandle_t)timer_id) != pdFALSE)
    {
        return osTimerRunning;
    }
    return osTimerStopped;
}

osStatus_t osTimerDelete(osTimerId_t timer_id)
{
    TimerHandle_t hTimer = (TimerHandle_t)timer_id;
    os2_timer_cb_t *cb;

    if (hTimer == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        return osErrorISR;
    }

    cb = (os2_timer_cb_t *)pvTimerGetTimerID(hTimer);
    (void)xTimerDelete(hTimer, portMAX_DELAY);
    if (cb != NULL)
    {
        vPortFree(cb);
    }
    return osOK;
}

/* ==========================================================================
 *  事件标志
 * ========================================================================== */

osEventFlagsId_t osEventFlagsNew(const osEventFlagsAttr_t *attr)
{
    EventGroupHandle_t hGroup;
    const char *name = NULL;
    StaticEventGroup_t *eg_mem = NULL;

    if (IS_IRQ())
    {
        return NULL;
    }
    if (attr != NULL)
    {
        name = attr->name;
        eg_mem = (StaticEventGroup_t *)attr->cb_mem;
    }

    if (eg_mem != NULL)
    {
        hGroup = xEventGroupCreateStatic(eg_mem);
    }
    else
    {
        hGroup = xEventGroupCreate();
    }
    (void)name; /* FreeRTOS 事件组不支持命名 */
    return (osEventFlagsId_t)hGroup;
}

uint32_t osEventFlagsSet(osEventFlagsId_t ef_id, uint32_t flags)
{
    EventGroupHandle_t hGroup = (EventGroupHandle_t)ef_id;

    if (hGroup == NULL)
    {
        return (uint32_t)osErrorParameter;
    }

    if (IS_IRQ())
    {
#if ((INCLUDE_xTimerPendFunctionCall == 1) && (configUSE_TIMERS == 1))
        BaseType_t yield = pdFALSE;
        (void)xEventGroupSetBitsFromISR(hGroup, (EventBits_t)flags, &yield);
        portYIELD_FROM_ISR(yield);
        return 0U;
#else
        return (uint32_t)osErrorResource;
#endif
    }
    return (uint32_t)xEventGroupSetBits(hGroup, (EventBits_t)flags);
}

uint32_t osEventFlagsClear(osEventFlagsId_t ef_id, uint32_t flags)
{
    EventGroupHandle_t hGroup = (EventGroupHandle_t)ef_id;

    if (hGroup == NULL)
    {
        return (uint32_t)osErrorParameter;
    }
    if (IS_IRQ())
    {
        return (uint32_t)osErrorISR;
    }
    return (uint32_t)xEventGroupClearBits(hGroup, (EventBits_t)flags);
}

uint32_t osEventFlagsGet(osEventFlagsId_t ef_id)
{
    EventGroupHandle_t hGroup = (EventGroupHandle_t)ef_id;

    if (hGroup == NULL)
    {
        return (uint32_t)osErrorParameter;
    }
    return (uint32_t)xEventGroupGetBits(hGroup);
}

uint32_t osEventFlagsWait(osEventFlagsId_t ef_id, uint32_t flags,
                          uint32_t options, uint32_t timeout)
{
    EventGroupHandle_t hGroup = (EventGroupHandle_t)ef_id;
    BaseType_t wait_all;
    BaseType_t clear_on_exit;
    EventBits_t bits;

    if (hGroup == NULL)
    {
        return (uint32_t)osErrorParameter;
    }
    if (IS_IRQ())
    {
        return (uint32_t)osErrorISR;
    }

    wait_all = (options & osFlagsWaitAll) ? pdTRUE : pdFALSE;
    clear_on_exit = (options & osFlagsNoClear) ? pdFALSE : pdTRUE;

    bits = xEventGroupWaitBits(hGroup, (EventBits_t)flags,
                               clear_on_exit, wait_all,
                               os2_timeout_to_ticks(timeout));

    /* FreeRTOS 超时返回时也会带回当前位，需据此判断是否真正满足条件 */
    if (wait_all != pdFALSE)
    {
        if ((bits & (EventBits_t)flags) != (EventBits_t)flags)
        {
            return (uint32_t)osErrorTimeout;
        }
    }
    else
    {
        if ((bits & (EventBits_t)flags) == 0U)
        {
            return (uint32_t)osErrorTimeout;
        }
    }
    return (uint32_t)bits;
}

osStatus_t osEventFlagsDelete(osEventFlagsId_t ef_id)
{
    EventGroupHandle_t hGroup = (EventGroupHandle_t)ef_id;

    if (hGroup == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    vEventGroupDelete(hGroup);
    return osOK;
}

/* ==========================================================================
 *  互斥锁
 * ========================================================================== */

osMutexId_t osMutexNew(const osMutexAttr_t *attr)
{
    SemaphoreHandle_t hMutex;
    uint32_t bits = (attr != NULL) ? attr->attr_bits : 0U;

    if (IS_IRQ())
    {
        return NULL;
    }

    if ((bits & osMutexRecursive) != 0U)
    {
        hMutex = xSemaphoreCreateRecursiveMutex();
    }
    else
    {
        hMutex = xSemaphoreCreateMutex();
    }
    return (osMutexId_t)hMutex;
}

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout)
{
    SemaphoreHandle_t hMutex = (SemaphoreHandle_t)mutex_id;
    BaseType_t rc;

    if (hMutex == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    rc = xSemaphoreTake(hMutex, os2_timeout_to_ticks(timeout));
    return (rc == pdPASS) ? osOK : osErrorTimeout;
}

osStatus_t osMutexRelease(osMutexId_t mutex_id)
{
    SemaphoreHandle_t hMutex = (SemaphoreHandle_t)mutex_id;

    if (hMutex == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    if (xSemaphoreGive(hMutex) != pdPASS)
    {
        return osErrorResource;
    }
    return osOK;
}

osThreadId_t osMutexGetOwner(osMutexId_t mutex_id)
{
    SemaphoreHandle_t hMutex = (SemaphoreHandle_t)mutex_id;

    if (hMutex == NULL)
    {
        return NULL;
    }
#if (INCLUDE_xSemaphoreGetMutexHolder == 1)
    return (osThreadId_t)xSemaphoreGetMutexHolder(hMutex);
#else
    (void)hMutex;
    return NULL;
#endif
}

osStatus_t osMutexDelete(osMutexId_t mutex_id)
{
    SemaphoreHandle_t hMutex = (SemaphoreHandle_t)mutex_id;

    if (hMutex == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    vSemaphoreDelete(hMutex);
    return osOK;
}

/* ==========================================================================
 *  信号量
 * ========================================================================== */

osSemaphoreId_t osSemaphoreNew(uint32_t max_count, uint32_t initial_count,
                               const osSemaphoreAttr_t *attr)
{
    SemaphoreHandle_t hSem;

    if (IS_IRQ())
    {
        return NULL;
    }
    if (initial_count > max_count)
    {
        initial_count = max_count;
    }

    if (max_count == 1U)
    {
        hSem = xSemaphoreCreateBinary();
        if ((hSem != NULL) && (initial_count == 1U))
        {
            (void)xSemaphoreGive(hSem);
        }
    }
    else
    {
        hSem = xSemaphoreCreateCounting((UBaseType_t)max_count,
                                        (UBaseType_t)initial_count);
    }
    (void)attr;
    return (osSemaphoreId_t)hSem;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout)
{
    SemaphoreHandle_t hSem = (SemaphoreHandle_t)semaphore_id;
    BaseType_t rc;

    if (hSem == NULL)
    {
        return osErrorParameter;
    }

    if (IS_IRQ())
    {
        if (timeout != 0U)
        {
            return osErrorISR;
        }
        BaseType_t yield = pdFALSE;
        rc = xSemaphoreTakeFromISR(hSem, &yield);
        portYIELD_FROM_ISR(yield);
    }
    else
    {
        rc = xSemaphoreTake(hSem, os2_timeout_to_ticks(timeout));
    }
    return (rc == pdPASS) ? osOK : osErrorTimeout;
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id)
{
    SemaphoreHandle_t hSem = (SemaphoreHandle_t)semaphore_id;
    BaseType_t rc;

    if (hSem == NULL)
    {
        return osErrorParameter;
    }

    if (IS_IRQ())
    {
        BaseType_t yield = pdFALSE;
        rc = xSemaphoreGiveFromISR(hSem, &yield);
        portYIELD_FROM_ISR(yield);
    }
    else
    {
        rc = xSemaphoreGive(hSem);
    }
    return (rc == pdPASS) ? osOK : osErrorResource;
}

uint32_t osSemaphoreGetCount(osSemaphoreId_t semaphore_id)
{
    SemaphoreHandle_t hSem = (SemaphoreHandle_t)semaphore_id;

    if (hSem == NULL)
    {
        return 0U;
    }
    return (uint32_t)uxSemaphoreGetCount(hSem);
}

osStatus_t osSemaphoreDelete(osSemaphoreId_t semaphore_id)
{
    SemaphoreHandle_t hSem = (SemaphoreHandle_t)semaphore_id;

    if (hSem == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    vSemaphoreDelete(hSem);
    return osOK;
}

/* ==========================================================================
 *  内存池
 *
 *  经典实现：空闲链表复用空闲块的内存存 next 指针，配一个计数信号量
 *  记录剩余块数。alloc 先 take 计数信号量，再在临界区里弹出空闲块；
 *  free 先 push 回空闲链表，再 give 计数信号量。
 * ========================================================================== */

typedef struct os2_mp_cb
{
    uint8_t *pool;     /* 块内存区 */
    uint32_t blk_size; /* 对齐后的块大小（字节） */
    uint32_t req_size; /* 用户请求的块大小（字节） */
    uint32_t blk_cnt;
    SemaphoreHandle_t space; /* 计数信号量，初值 blk_cnt */
    uint8_t *free_head;      /* 空闲链表头（复用块内存） */
} os2_mp_cb_t;

osMemoryPoolId_t osMemoryPoolNew(uint32_t block_count, uint32_t block_size,
                                 const osMemoryPoolAttr_t *attr)
{
    os2_mp_cb_t *cb;
    uint32_t aligned;
    uint32_t i;
    uint8_t *p;

    if (IS_IRQ() || (block_count == 0U) || (block_size == 0U))
    {
        return NULL;
    }

    /* 块大小向上对齐到 sizeof(void*)，且至少能存下一个 next 指针 */
    aligned = (block_size + (uint32_t)sizeof(void *) - 1U) &
              ~((uint32_t)sizeof(void *) - 1U);
    if (aligned < sizeof(void *))
    {
        aligned = sizeof(void *);
    }

    cb = (os2_mp_cb_t *)pvPortMalloc(sizeof(os2_mp_cb_t));
    if (cb == NULL)
    {
        return NULL;
    }
    memset(cb, 0, sizeof(os2_mp_cb_t));

    cb->blk_size = aligned;
    cb->req_size = block_size;
    cb->blk_cnt = block_count;

    cb->space = xSemaphoreCreateCounting((UBaseType_t)block_count,
                                         (UBaseType_t)block_count);
    if (cb->space == NULL)
    {
        vPortFree(cb);
        return NULL;
    }

    cb->pool = (uint8_t *)pvPortMalloc(aligned * block_count);
    if (cb->pool == NULL)
    {
        vSemaphoreDelete(cb->space);
        vPortFree(cb);
        return NULL;
    }

    /* 初始化空闲链表 */
    cb->free_head = cb->pool;
    p = cb->pool;
    for (i = 0U; i < block_count; i++)
    {
        void **slot = (void **)p;
        *slot = (i + 1U < block_count) ? (void *)(p + aligned) : NULL;
        p += aligned;
    }

    (void)attr;
    return (osMemoryPoolId_t)cb;
}

void *osMemoryPoolAlloc(osMemoryPoolId_t mp_id, uint32_t timeout)
{
    os2_mp_cb_t *cb = (os2_mp_cb_t *)mp_id;
    void *blk;
    BaseType_t rc;

    if (cb == NULL)
    {
        return NULL;
    }

    if (IS_IRQ())
    {
        if (timeout != 0U)
        {
            return NULL; /* ISR 中不可阻塞等待 */
        }
        BaseType_t yield = pdFALSE;
        rc = xSemaphoreTakeFromISR(cb->space, &yield);
        portYIELD_FROM_ISR(yield);
    }
    else
    {
        rc = xSemaphoreTake(cb->space, os2_timeout_to_ticks(timeout));
    }
    if (rc != pdPASS)
    {
        return NULL;
    }

    /* 从空闲链表弹出（临界区保护） */
    {
        UBaseType_t cs_saved = 0U;

        if (IS_IRQ())
        {
            OS2_ISR_CRITICAL_ENTER(cs_saved);
            blk = cb->free_head;
            if (blk != NULL)
            {
                cb->free_head = *(void **)blk;
            }
            OS2_ISR_CRITICAL_EXIT(cs_saved);
        }
        else
        {
            OS2_CRITICAL_ENTER();
            blk = cb->free_head;
            if (blk != NULL)
            {
                cb->free_head = *(void **)blk;
            }
            OS2_CRITICAL_EXIT();
        }
    }

    return blk;
}

osStatus_t osMemoryPoolFree(osMemoryPoolId_t mp_id, void *block)
{
    os2_mp_cb_t *cb = (os2_mp_cb_t *)mp_id;

    if ((cb == NULL) || (block == NULL))
    {
        return osErrorParameter;
    }

    /* push 回空闲链表 */
    if (IS_IRQ())
    {
        UBaseType_t cs_saved = 0U;

        OS2_ISR_CRITICAL_ENTER(cs_saved);
        *(void **)block = cb->free_head;
        cb->free_head = block;
        OS2_ISR_CRITICAL_EXIT(cs_saved);

        BaseType_t yield = pdFALSE;
        (void)xSemaphoreGiveFromISR(cb->space, &yield);
        portYIELD_FROM_ISR(yield);
    }
    else
    {
        OS2_CRITICAL_ENTER();
        *(void **)block = cb->free_head;
        cb->free_head = block;
        OS2_CRITICAL_EXIT();

        (void)xSemaphoreGive(cb->space);
    }
    return osOK;
}

uint32_t osMemoryPoolGetCapacity(osMemoryPoolId_t mp_id)
{
    os2_mp_cb_t *cb = (os2_mp_cb_t *)mp_id;
    return (cb != NULL) ? cb->blk_cnt : 0U;
}

uint32_t osMemoryPoolGetBlockSize(osMemoryPoolId_t mp_id)
{
    os2_mp_cb_t *cb = (os2_mp_cb_t *)mp_id;
    return (cb != NULL) ? cb->req_size : 0U;
}

uint32_t osMemoryPoolGetCount(osMemoryPoolId_t mp_id)
{
    os2_mp_cb_t *cb = (os2_mp_cb_t *)mp_id;
    uint32_t used;

    if (cb == NULL)
    {
        return 0U;
    }
    used = cb->blk_cnt - (uint32_t)uxSemaphoreGetCount(cb->space);
    return used;
}

uint32_t osMemoryPoolGetSpace(osMemoryPoolId_t mp_id)
{
    os2_mp_cb_t *cb = (os2_mp_cb_t *)mp_id;
    return (cb != NULL) ? (uint32_t)uxSemaphoreGetCount(cb->space) : 0U;
}

osStatus_t osMemoryPoolDelete(osMemoryPoolId_t mp_id)
{
    os2_mp_cb_t *cb = (os2_mp_cb_t *)mp_id;

    if (cb == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    vSemaphoreDelete(cb->space);
    vPortFree(cb->pool);
    vPortFree(cb);
    return osOK;
}

/* ==========================================================================
 *  消息队列
 * ========================================================================== */

typedef struct os2_mq_cb
{
    QueueHandle_t queue;
    uint32_t msg_size;
    uint32_t capacity;
} os2_mq_cb_t;

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size,
                                     const osMessageQueueAttr_t *attr)
{
    os2_mq_cb_t *cb;
    QueueHandle_t q;

    if (IS_IRQ() || (msg_count == 0U) || (msg_size == 0U))
    {
        return NULL;
    }

    cb = (os2_mq_cb_t *)pvPortMalloc(sizeof(os2_mq_cb_t));
    if (cb == NULL)
    {
        return NULL;
    }
    memset(cb, 0, sizeof(os2_mq_cb_t));

    q = xQueueCreate((UBaseType_t)msg_count, (UBaseType_t)msg_size);
    if (q == NULL)
    {
        vPortFree(cb);
        return NULL;
    }

    cb->queue = q;
    cb->msg_size = msg_size;
    cb->capacity = msg_count;
    (void)attr;
    return (osMessageQueueId_t)cb;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr,
                             uint8_t msg_prio, uint32_t timeout)
{
    os2_mq_cb_t *cb = (os2_mq_cb_t *)mq_id;
    BaseType_t rc;

    if ((cb == NULL) || (msg_ptr == NULL))
    {
        return osErrorParameter;
    }

    /* FreeRTOS 不支持队列内消息优先级，仅提供前/后插入：
     *   msg_prio == 0 -> 队尾，msg_prio > 0 -> 队首 */
    if (IS_IRQ())
    {
        if (timeout != 0U)
        {
            return osErrorISR;
        }
        BaseType_t yield = pdFALSE;
        if (msg_prio != 0U)
        {
            rc = xQueueSendToFrontFromISR(cb->queue, msg_ptr, &yield);
        }
        else
        {
            rc = xQueueSendToBackFromISR(cb->queue, msg_ptr, &yield);
        }
        portYIELD_FROM_ISR(yield);
    }
    else
    {
        if (msg_prio != 0U)
        {
            rc = xQueueSendToFront(cb->queue, msg_ptr, os2_timeout_to_ticks(timeout));
        }
        else
        {
            rc = xQueueSendToBack(cb->queue, msg_ptr, os2_timeout_to_ticks(timeout));
        }
    }
    if (rc != pdPASS)
    {
        return (timeout == 0U) ? osErrorResource : osErrorTimeout;
    }
    return osOK;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr,
                             uint8_t *msg_prio, uint32_t timeout)
{
    os2_mq_cb_t *cb = (os2_mq_cb_t *)mq_id;
    BaseType_t rc;

    if ((cb == NULL) || (msg_ptr == NULL))
    {
        return osErrorParameter;
    }

    if (IS_IRQ())
    {
        if (timeout != 0U)
        {
            return osErrorISR;
        }
        BaseType_t yield = pdFALSE;
        rc = xQueueReceiveFromISR(cb->queue, msg_ptr, &yield);
        portYIELD_FROM_ISR(yield);
    }
    else
    {
        rc = xQueueReceive(cb->queue, msg_ptr, os2_timeout_to_ticks(timeout));
    }
    if (rc != pdPASS)
    {
        return (timeout == 0U) ? osErrorResource : osErrorTimeout;
    }
    if (msg_prio != NULL)
    {
        *msg_prio = 0U; /* FreeRTOS 无消息优先级，恒为 0 */
    }
    return osOK;
}

uint32_t osMessageQueueGetCapacity(osMessageQueueId_t mq_id)
{
    os2_mq_cb_t *cb = (os2_mq_cb_t *)mq_id;
    return (cb != NULL) ? cb->capacity : 0U;
}

uint32_t osMessageQueueGetMsgSize(osMessageQueueId_t mq_id)
{
    os2_mq_cb_t *cb = (os2_mq_cb_t *)mq_id;
    return (cb != NULL) ? cb->msg_size : 0U;
}

uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id)
{
    os2_mq_cb_t *cb = (os2_mq_cb_t *)mq_id;
    if (cb == NULL)
    {
        return 0U;
    }
    if (IS_IRQ())
    {
        return (uint32_t)uxQueueMessagesWaitingFromISR(cb->queue);
    }
    return (uint32_t)uxQueueMessagesWaiting(cb->queue);
}

uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id)
{
    os2_mq_cb_t *cb = (os2_mq_cb_t *)mq_id;
    uint32_t cnt;
    if (cb == NULL)
    {
        return 0U;
    }
    cnt = osMessageQueueGetCount(mq_id);
    return (cnt < cb->capacity) ? (cb->capacity - cnt) : 0U;
}

osStatus_t osMessageQueueReset(osMessageQueueId_t mq_id)
{
    os2_mq_cb_t *cb = (os2_mq_cb_t *)mq_id;

    if (cb == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    (void)xQueueReset(cb->queue);
    return osOK;
}

osStatus_t osMessageQueueDelete(osMessageQueueId_t mq_id)
{
    os2_mq_cb_t *cb = (os2_mq_cb_t *)mq_id;

    if (cb == NULL)
    {
        return osErrorParameter;
    }
    if (IS_IRQ())
    {
        return osErrorISR;
    }
    vQueueDelete(cb->queue);
    vPortFree(cb);
    return osOK;
}

/* ==========================================================================
 *  ESP32 扩展：核心亲和性（定义见 esp_cmsis_os2_ext.h）
 * ========================================================================== */

#ifdef ESP_PLATFORM

/*
 *  核心亲和性 API 只在 SMP 内核（configNUM_CORES > 1）下存在。
 *  ESP32-S2 / C3 / C6 / H2 是单核芯片，ESP-IDF 编译的 FreeRTOS
 *  没有编译进 vTaskCoreAffinitySet/Get，必须做条件编译。
 */
#ifndef configNUM_CORES
#define configNUM_CORES 1
#endif

#if configNUM_CORES > 1

BaseType_t osThreadSetCoreAffinity(osThreadId_t thread_id, UBaseType_t core_mask)
{
    if (thread_id == NULL)
    {
        thread_id = (osThreadId_t)xTaskGetCurrentTaskHandle();
    }
    return vTaskCoreAffinitySet((TaskHandle_t)thread_id, core_mask);
}

#else /* 单核芯片：mask 必须包含核 0，其余视为失败 */

BaseType_t osThreadSetCoreAffinity(osThreadId_t thread_id, UBaseType_t core_mask)
{
    (void)thread_id; /* 单核上所有任务都在核 0，无需真正修改 */
    return (core_mask & 1U) ? pdPASS : pdFAIL;
}

#endif /* configNUM_CORES > 1 */

uint32_t osThreadGetCoreId(void)
{
    return (uint32_t)xPortGetCoreID();
}

#endif /* ESP_PLATFORM */

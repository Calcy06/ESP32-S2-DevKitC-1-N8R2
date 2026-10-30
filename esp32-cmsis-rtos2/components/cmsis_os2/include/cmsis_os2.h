/* --------------------------------------------------------------------------
 *  cmsis_os2.h  --  CMSIS-RTOS2 v2 API
 *  ESP-IDF (Xtensa / RISC-V) adaption
 *
 *  Derived from ARM CMSIS-RTOS2 (ARM-software/CMSIS_6), Apache-2.0 License.
 *
 *  说明：本文件与 ARM 官方 cmsis_os2.h 保持 100% 源码级兼容 ——
 *  你在 STM32 上写的 os* 代码，可以直接 COPY 到 ESP32 工程使用。
 *  实现侧的 ESP32 特有差异全部封装在 cmsis_os2.c 里。
 * -------------------------------------------------------------------------- */

#ifndef CMSIS_OS2_H_
#define CMSIS_OS2_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 版本 ============================ */

#define osVersionAPI          20010003    /* 2.1.3 */
#define osVersionKernel       20010003
#define osKernelSystemId      "FreeRTOS"

#define osCMSIS               0x00020000U /* CMSIS-RTOS2 2.x 标识 */

/* ========================= 特性指示 ========================= */

#define osFeature_Timer       1U          /* 定时器: 1 = 支持 */
#define osFeature_EventFlags  1U
#define osFeature_Mutex       1U
#define osFeature_Semaphore   32U         /* 信号量最大计数 */
#define osFeature_Pool        1U          /* 内存池 */
#define osFeature_MessageQ    1U          /* 消息队列 */
#define osFeature_MailQ       0U

#define osThreadFlagsLimit    32U         /* 线程标志位数 */
#define osEventFlagsLimit     31U         /* 事件标志可用位数 (FreeRTOS 保留高 1 位) */
#define osMutexFlagsLimit     3U
#define osSemaphoreFlagsLimit 3U
#define osPoolFlagsLimit      3U
#define osMessageQFlagsLimit  3U

/* =========================== 通用状态码 ============================ */

#define osFlagsWaitAny        0x00000000U /* 等待任意一个位 */
#define osFlagsWaitAll        0x00000001U /* 等待全部位 */
#define osFlagsNoClear        0x00000002U /* 取出后不清除 */

#define osThreadDetached      0x00000000U
#define osThreadJoinable      0x00000001U

#define osMutexRecursive      0x00000001U
#define osMutexPrioInherit    0x00000002U
#define osMutexRobust         0x00000008U

#define osTimerOnce           0x00000000U
#define osTimerPeriodic       0x00000001U

#define osWaitForever         0xFFFFFFFFU

typedef enum {
    osOK                    =  0,
    osError                 = -1,
    osErrorTimeout          = -2,
    osErrorResource         = -3,
    osErrorParameter        = -4,
    osErrorNoMemory         = -5,
    osErrorISR              = -6,
    osStatusReserved        = 0x7FFFFFFF
} osStatus_t;

/* ============================ 优先级 ============================
 * 注意: CMSIS 的优先级数值域为 [1, 56]，与 FreeRTOS 的 [0, configMAX_PRIORITIES-1]
 *       完全不同。实现层做线性映射，切勿把两边数值混用。
 * ---------------------------------------------------------------- */

typedef enum {
    osPriorityNone          =  0,
    osPriorityIdle          =  1,
    osPriorityLow           =  8,
    osPriorityLow1          =  9,
    osPriorityLow2          = 10,
    osPriorityLow3          = 11,
    osPriorityLow4          = 12,
    osPriorityLow5          = 13,
    osPriorityLow6          = 14,
    osPriorityLow7          = 15,
    osPriorityBelowNormal   = 16,
    osPriorityBelowNormal1  = 17,
    osPriorityBelowNormal2  = 18,
    osPriorityBelowNormal3  = 19,
    osPriorityBelowNormal4  = 20,
    osPriorityBelowNormal5  = 21,
    osPriorityBelowNormal6  = 22,
    osPriorityBelowNormal7  = 23,
    osPriorityNormal        = 24,
    osPriorityNormal1       = 25,
    osPriorityNormal2       = 26,
    osPriorityNormal3       = 27,
    osPriorityNormal4       = 28,
    osPriorityNormal5       = 29,
    osPriorityNormal6       = 30,
    osPriorityNormal7       = 31,
    osPriorityAboveNormal   = 32,
    osPriorityAboveNormal1  = 33,
    osPriorityAboveNormal2  = 34,
    osPriorityAboveNormal3  = 35,
    osPriorityAboveNormal4  = 36,
    osPriorityAboveNormal5  = 37,
    osPriorityAboveNormal6  = 38,
    osPriorityAboveNormal7  = 39,
    osPriorityHigh          = 40,
    osPriorityHigh1         = 41,
    osPriorityHigh2         = 42,
    osPriorityHigh3         = 43,
    osPriorityHigh4         = 44,
    osPriorityHigh5         = 45,
    osPriorityHigh6         = 46,
    osPriorityHigh7         = 47,
    osPriorityRealtime      = 48,
    osPriorityRealtime1     = 49,
    osPriorityRealtime2     = 50,
    osPriorityRealtime3     = 51,
    osPriorityRealtime4     = 52,
    osPriorityRealtime5     = 53,
    osPriorityRealtime6     = 54,
    osPriorityRealtime7     = 55,
    osPriorityISR           = 56,
    osPriorityError         = -1,
    osPriorityReserved      = 0x7FFFFFFF
} osPriority_t;

/* ========================== 状态枚举 ============================ */

typedef enum {
    osThreadInactive        =  0,
    osThreadReady           =  1,
    osThreadRunning         =  2,
    osThreadBlocked         =  3,
    osThreadTerminated      =  4,
    osThreadError           = -1,
    osThreadReserved        = 0x7FFFFFFF
} osThreadState_t;

typedef enum {
    osKernelInactive        =  0,
    osKernelReady           =  1,
    osKernelRunning         =  2,
    osKernelLocked          =  3,
    osKernelSuspended       =  4,
    osKernelError           = -1,
    osKernelReserved        = 0x7FFFFFFF
} osKernelState_t;

typedef enum {
    osTimerRunning          =  1,
    osTimerStopped          =  0,
    osTimerError            = -1,
    osTimerReserved         = 0x7FFFFFFF
} osTimerState_t;

/* =========================== 版本信息 =========================== */

typedef struct {
    uint32_t                api;         /* API 版本 */
    uint32_t                kernel;      /* 内核版本 */
} osVersion_t;

/* ============================ 内核信息 ============================ */

typedef struct {
    const char             *name;        /* 内核名称 */
    const osVersion_t      *version;     /* 版本 */
} osKernelInfo_t;

/* ======================= 线程属性（创建参数） ======================= */

typedef struct {
    const char             *name;        /* 线程名（调试用） */
    uint32_t                attr_bits;   /* osThreadJoinable 等 */
    void                   *cb_mem;      /* 用户提供的控制块内存（静态创建） */
    uint32_t                cb_size;     /* cb_mem 大小 */
    void                   *stack_mem;   /* 用户提供的栈内存（静态创建） */
    uint32_t                stack_size;  /* 栈大小（字节） */
    osPriority_t            priority;    /* 初始优先级 */
    int32_t                 tz_module;   /* 未使用 */
    uint32_t                reserved;    /* 保留 */
} osThreadAttr_t;

/* ======================= 线程标志（任务通知） ======================= */

typedef void               (*osThreadFunc_t)(void *argument);

typedef uint32_t            osThreadFlags_t;
typedef void                *osThreadId_t;

/* ======================= 定时器 ======================= */

typedef void               (*osTimerFunc_t)(void *argument);
typedef void                *osTimerId_t;

/* osTimerNew 的 type 参数：osTimerOnce / osTimerPeriodic */
typedef uint32_t             osTimerType_t;

typedef struct {
    const char             *name;
    uint32_t                attr_bits;   /* osTimerPeriodic 等 */
    void                   *cb_mem;
    uint32_t                cb_size;
} osTimerAttr_t;

/* ======================= 事件标志 ======================= */

typedef void                *osEventFlagsId_t;

typedef struct {
    const char             *name;
    uint32_t                attr_bits;
    void                   *cb_mem;
    uint32_t                cb_size;
} osEventFlagsAttr_t;

/* ======================= 互斥锁 ======================= */

typedef void                *osMutexId_t;

typedef struct {
    const char             *name;
    uint32_t                attr_bits;   /* osMutexRecursive 等 */
    void                   *cb_mem;
    uint32_t                cb_size;
} osMutexAttr_t;

/* ======================= 信号量 ======================= */

typedef void                *osSemaphoreId_t;

typedef struct {
    const char             *name;
    uint32_t                attr_bits;
    void                   *cb_mem;
    uint32_t                cb_size;
} osSemaphoreAttr_t;

/* ======================= 内存池 ======================= */

typedef void                *osMemoryPoolId_t;

typedef struct {
    const char             *name;
    uint32_t                attr_bits;
    void                   *cb_mem;
    uint32_t                cb_size;
    void                   *mp_mem;      /* 内存池数据区（静态创建） */
    uint32_t                mp_size;     /* 数据区大小 */
} osMemoryPoolAttr_t;

/* ======================= 消息队列 ======================= */

typedef void                *osMessageQueueId_t;

typedef struct {
    const char             *name;
    uint32_t                attr_bits;
    void                   *cb_mem;
    uint32_t                cb_size;
    void                   *mq_mem;      /* 队列存储区（静态创建） */
    uint32_t                mq_size;     /* 存储区大小 */
} osMessageQueueAttr_t;

/* =====================================================================
 *  内核控制
 * ===================================================================== */

osStatus_t    osKernelInitialize       (void);
osStatus_t    osKernelGetInfo          (osVersion_t *version, char *id_buf, uint32_t id_size);
osKernelState_t osKernelGetState       (void);
osStatus_t    osKernelStart            (void);
int32_t       osKernelLock             (void);
int32_t       osKernelUnlock           (void);
int32_t       osKernelRestoreLock      (int32_t lock);
uint32_t      osKernelSuspend          (void);
void          osKernelResume           (uint32_t sleep_ticks);
uint32_t      osKernelGetTickCount     (void);
uint32_t      osKernelGetTickFreq      (void);
uint32_t      osKernelGetSysTimerCount (void);
uint32_t      osKernelGetSysTimerFreq  (void);

/* =====================================================================
 *  线程管理
 * ===================================================================== */

osThreadId_t  osThreadNew              (osThreadFunc_t func, void *argument, const osThreadAttr_t *attr);
const char   *osThreadGetName          (osThreadId_t thread_id);
osThreadId_t  osThreadGetId            (void);
osThreadState_t osThreadGetState       (osThreadId_t thread_id);
osStatus_t    osThreadSetPriority      (osThreadId_t thread_id, osPriority_t priority);
osPriority_t  osThreadGetPriority      (osThreadId_t thread_id);
osStatus_t    osThreadYield            (void);
osStatus_t    osThreadSuspend          (osThreadId_t thread_id);
osStatus_t    osThreadResume           (osThreadId_t thread_id);
osStatus_t    osThreadDetach           (osThreadId_t thread_id);
osStatus_t    osThreadJoin             (osThreadId_t thread_id);
void          osThreadExit             (void);
osStatus_t    osThreadTerminate        (osThreadId_t thread_id);
uint32_t      osThreadGetStackSize     (osThreadId_t thread_id);
uint32_t      osThreadGetStackSpace    (osThreadId_t thread_id);
uint32_t      osThreadGetCount         (void);
uint32_t      osThreadEnumerate        (osThreadId_t *thread_array, uint32_t array_items);

/* =====================================================================
 *  线程标志（基于 FreeRTOS 任务通知）
 * ===================================================================== */

uint32_t      osThreadFlagsSet         (osThreadId_t thread_id, uint32_t flags);
uint32_t      osThreadFlagsClear       (uint32_t flags);
uint32_t      osThreadFlagsGet         (void);
uint32_t      osThreadFlagsWait        (uint32_t flags, uint32_t options, uint32_t timeout);

/* =====================================================================
 *  通用等待（延迟）
 * ===================================================================== */

osStatus_t    osDelay                  (uint32_t ticks);
osStatus_t    osDelayUntil             (uint32_t ticks);

/* =====================================================================
 *  软件定时器
 * ===================================================================== */

osTimerId_t   osTimerNew               (osTimerFunc_t func, osTimerType_t type, void *argument, const osTimerAttr_t *attr);
const char   *osTimerGetName           (osTimerId_t timer_id);
osStatus_t    osTimerStart             (osTimerId_t timer_id, uint32_t ticks);
osStatus_t    osTimerStop              (osTimerId_t timer_id);
uint32_t      osTimerIsRunning         (osTimerId_t timer_id);
osStatus_t    osTimerDelete            (osTimerId_t timer_id);

/* =====================================================================
 *  事件标志
 * ===================================================================== */

osEventFlagsId_t osEventFlagsNew       (const osEventFlagsAttr_t *attr);
uint32_t      osEventFlagsSet          (osEventFlagsId_t ef_id, uint32_t flags);
uint32_t      osEventFlagsClear        (osEventFlagsId_t ef_id, uint32_t flags);
uint32_t      osEventFlagsGet          (osEventFlagsId_t ef_id);
uint32_t      osEventFlagsWait         (osEventFlagsId_t ef_id, uint32_t flags, uint32_t options, uint32_t timeout);
osStatus_t    osEventFlagsDelete       (osEventFlagsId_t ef_id);

/* =====================================================================
 *  互斥锁
 * ===================================================================== */

osMutexId_t   osMutexNew               (const osMutexAttr_t *attr);
osStatus_t    osMutexAcquire           (osMutexId_t mutex_id, uint32_t timeout);
osStatus_t    osMutexRelease           (osMutexId_t mutex_id);
osThreadId_t  osMutexGetOwner          (osMutexId_t mutex_id);
osStatus_t    osMutexDelete            (osMutexId_t mutex_id);

/* =====================================================================
 *  信号量
 * ===================================================================== */

osSemaphoreId_t osSemaphoreNew         (uint32_t max_count, uint32_t initial_count, const osSemaphoreAttr_t *attr);
osStatus_t    osSemaphoreAcquire       (osSemaphoreId_t semaphore_id, uint32_t timeout);
osStatus_t    osSemaphoreRelease       (osSemaphoreId_t semaphore_id);
uint32_t      osSemaphoreGetCount      (osSemaphoreId_t semaphore_id);
osStatus_t    osSemaphoreDelete        (osSemaphoreId_t semaphore_id);

/* =====================================================================
 *  内存池
 * ===================================================================== */

osMemoryPoolId_t osMemoryPoolNew       (uint32_t block_count, uint32_t block_size, const osMemoryPoolAttr_t *attr);
void         *osMemoryPoolAlloc        (osMemoryPoolId_t mp_id, uint32_t timeout);
osStatus_t    osMemoryPoolFree         (osMemoryPoolId_t mp_id, void *block);
uint32_t      osMemoryPoolGetCapacity  (osMemoryPoolId_t mp_id);
uint32_t      osMemoryPoolGetBlockSize (osMemoryPoolId_t mp_id);
uint32_t      osMemoryPoolGetCount     (osMemoryPoolId_t mp_id);
uint32_t      osMemoryPoolGetSpace     (osMemoryPoolId_t mp_id);
osStatus_t    osMemoryPoolDelete       (osMemoryPoolId_t mp_id);

/* =====================================================================
 *  消息队列
 * ===================================================================== */

osMessageQueueId_t osMessageQueueNew   (uint32_t msg_count, uint32_t msg_size, const osMessageQueueAttr_t *attr);
osStatus_t    osMessageQueuePut        (osMessageQueueId_t mq_id, const void *msg_ptr, uint8_t msg_prio, uint32_t timeout);
osStatus_t    osMessageQueueGet        (osMessageQueueId_t mq_id, void *msg_ptr, uint8_t *msg_prio, uint32_t timeout);
uint32_t      osMessageQueueGetCapacity(osMessageQueueId_t mq_id);
uint32_t      osMessageQueueGetMsgSize (osMessageQueueId_t mq_id);
uint32_t      osMessageQueueGetCount   (osMessageQueueId_t mq_id);
uint32_t      osMessageQueueGetSpace   (osMessageQueueId_t mq_id);
osStatus_t    osMessageQueueReset      (osMessageQueueId_t mq_id);
osStatus_t    osMessageQueueDelete     (osMessageQueueId_t mq_id);

#ifdef __cplusplus
}
#endif

#endif /* CMSIS_OS2_H_ */

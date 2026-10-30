/* --------------------------------------------------------------------------
 *  esp_cmsis_os2_ext.h  --  ESP32 专有扩展（非 CMSIS-RTOS2 标准）
 *
 *  CMSIS-RTOS2 规范里没有「多核亲和性」的概念，而 ESP32 是双核 SMP。
 *  为了让 os* 写法也能表达「这个任务钉到哪个核」，这里提供一组可选的
 *  扩展 API。它们不破坏标准兼容性 —— 不用它们一切正常工作。
 * -------------------------------------------------------------------------- */

#ifndef ESP_CMSIS_OS2_EXT_H_
#define ESP_CMSIS_OS2_EXT_H_

#include "cmsis_os2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ESP32 核心编号 */
#define OS2_CORE_PRO_CPU      0          /* Protocol CPU (CPU0) —— Wi-Fi/BT 默认在此 */
#define OS2_CORE_APP_CPU      1          /* Application CPU (CPU1) */
#define OS2_CORE_NO_AFFINITY  tskNO_AFFINITY   /* 不绑核，两核皆可 */

/* 默认 application task 栈大小（字节）。
 * ESP32 的任务栈比 Cortex-M 通常要开得更大：每个任务还要承担
 * libc / printf / 浮点上下文等开销，小于 2048 很容易溢出。 */
#ifndef OS2_DEFAULT_STACK_BYTES
#define OS2_DEFAULT_STACK_BYTES     4096U
#endif

/* 创建任务并可指定核心亲和性（xCoreID 取值见上）。
 * 除 xCoreID 外，其余参数与标准 osThreadNew 完全一致。 */
osThreadId_t osThreadNewCore(osThreadFunc_t func,
                             void          *argument,
                             const osThreadAttr_t *attr,
                             BaseType_t     xCoreID);

/* 修改一个已有任务的核心亲和性（仅 ESP32 双核有效）。
 * 返回 pdPASS / pdFAIL。 */
BaseType_t   osThreadSetCoreAffinity(osThreadId_t thread_id, UBaseType_t core_mask);

/* 查询当前任务运行在哪个核上，返回 0 / 1 */
uint32_t     osThreadGetCoreId(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_CMSIS_OS2_EXT_H_ */

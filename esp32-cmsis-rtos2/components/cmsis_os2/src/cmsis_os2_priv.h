/* --------------------------------------------------------------------------
 *  cmsis_os2_priv.h  --  实现层内部定义，用户不应 include 此文件
 * -------------------------------------------------------------------------- */

#ifndef CMSIS_OS2_PRIV_H_
#define CMSIS_OS2_PRIV_H_

#include "cmsis_os2.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#include "FreeRTOS.h"
#include "task.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef OS2_DEFAULT_STACK_BYTES
#define OS2_DEFAULT_STACK_BYTES     4096U
#endif

/* 线程资源注册表节点。
 *
 * 之所以不用 FreeRTOS 的 TLS (vTaskSetThreadLocalStoragePointer) 存放本结构，
 * 是因为 ESP-IDF 里 configNUM_THREAD_LOCAL_STORAGE_POINTERS 可能只有 1 个槽，
 * 且 IDF 内部组件也会占用。用一张受临界区保护的小链表更可靠。
 */
typedef struct os2_thread_cb {
    struct os2_thread_cb * next;
    TaskHandle_t           task;
    uint32_t               stack_size;   /* 用户请求的栈大小（字节） */
} os2_thread_cb_t;

/* --- 注册表操作（内部） --- */
os2_thread_cb_t * os2_thread_reg_alloc  (void);                 /* 取一个已入表的空节点 */
os2_thread_cb_t * os2_thread_reg_find   (TaskHandle_t task);
void              os2_thread_reg_remove (TaskHandle_t task);

/* osThreadNewCore 定义在 esp_cmsis_os2_ext.h（ESP 版）；
 * 非 ESP 环境（如本机验证）下没有那个头，这里补一份原型避免出现隐式声明。 */
osThreadId_t      osThreadNewCore       (osThreadFunc_t func,
                                         void          *argument,
                                         const osThreadAttr_t *attr,
                                         BaseType_t     xCoreID);

#ifdef __cplusplus
}
#endif

#endif /* CMSIS_OS2_PRIV_H_ */

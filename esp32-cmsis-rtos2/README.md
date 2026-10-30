# 在 ESP32 上使用 CMSIS-RTOS2（`os*` 开头函数）

STM32 上写惯了 `osThreadNew` / `osMutexAcquire` / `osMessageQueuePut`，
想在 ESP32 上继续用同一套写法 —— 本组件就是为此写的：**一个跑在 ESP-IDF FreeRTOS 上的 CMSIS-RTOS2 v2 实现层**。

它在底层就是一层薄薄的翻译，底下仍然是队列 + 循环双向链表，
所以性能损耗极小，本质只是换了一套 API 命名。

```
应用层            osThreadNew / osMessageQueuePut / osMutexAcquire / ...   ← CMSIS-RTOS2
                                    ↓  cmsis_os2.c（本组件）
ESP-IDF FreeRTOS  xTaskCreatePinnedToCore / xQueueSend / xSemaphoreTake
                                    ↓
硬件              Xtensa LX6/LX7 或 RISC-V 双核
```

---

## 一、集成步骤

### 方式 A：直接复制（推荐）

把整个 `components/cmsis_os2` 目录拷进你的工程：

```text
your_project/
├── CMakeLists.txt
├── components/
│   └── cmsis_os2/
│       ├── CMakeLists.txt
│       ├── idf_component.yml
│       ├── include/
│       │   ├── cmsis_os2.h
│       │   └── esp_cmsis_os2_ext.h
│       └── src/
│           ├── cmsis_os2.c
│           └── cmsis_os2_priv.h
└── main/
    ├── CMakeLists.txt
    └── main.c
```

### 方式 B：引用本仓库路径

在你的顶层 `CMakeLists.txt` 里加一行：

```cmake
set(EXTRA_COMPONENT_DIRS /path/to/esp32-cmsis-rtos2/components)
```

### 然后：让你的组件依赖它

`main/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES cmsis_os2)
```

代码里正常 `#include "cmsis_os2.h"` 即可。

### 编译

```bash
idf.py set-target esp32s3      # 或 esp32 / esp32c3 / esp32c6 / esp32p4 ...
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

示例工程在 `examples/hello_os2/`，可直接 `idf.py build flash monitor`。

---

## 二、ESP32 与 STM32 的六处关键差异（务必先看）

这些是本适配层替你处理好的东西，但你必须知道它们的存在：

| # | 差异 | STM32 (CubeMX) | ESP32 (ESP-IDF) | 本层的处理 |
|---|---|---|---|---|
| 1 | **不能启动调度器** | `osKernelStart()` → `vTaskStartScheduler()` | 禁止调用，`app_main()` 之前调度器已在运行 | `osKernelStart()` 退化为幂等的状态确认，直接返回 `osOK` |
| 2 | **入口函数** | `int main(void)` | `void app_main(void)`，且可以返回 | 你仍需把 `main` 改成 `app_main` |
| 3 | **中断上下文判别** | 读 ARM 的 `IPSR` 寄存器 | Xtensa / RISC-V 没有 IPSR | 改用 ESP-IDF 的 `xPortInIsrContext()` |
| 4 | **栈大小单位** | vanilla FreeRTOS 用「字」(4 字节) | ESP-IDF 改为「字节」 | 与 CMSIS 的 `stack_size` 单位恰好一致，无需换算 |
| 5 | **多核** | 单核 | 双核 SMP（仅 ESP32 / S3 / P4） | 提供 `osThreadNewCore()` 绑核扩展 |
| 6 | **调度器挂起语义** | `vTaskSuspendAll()` 全局生效 | **只挂起调用的那一个核**，另一核照跑 | 见下方严重警告 |

### ⚠️ 关于第 6 点的严重警告

`osKernelLock()` / `osKernelUnlock()` 在 ESP32 上映射到 `vTaskSuspendAll()` /
`xTaskResumeAll()`，而它们**只作用于当前核**。

**因此它们不能用来做跨核互斥。** 若要保护共享资源，请明确使用互斥量：

```c
osMutexId_t m = osMutexNew(NULL);

osMutexAcquire(m, osWaitForever);
/* 临界区 —— 两核都安全 */
osMutexRelease(m);
```

同理，跨核共享变量上的 `volatile` 也起不到同步作用（SMP 有重排序和 cache 一致性问题），
要么用互斥量，要么用 `portMUX_TYPE` 自旋锁。

### 关于优先级

CMSIS 的优先级是 1~56 的整数（`osPriorityNormal` = 24），ESP-IDF 的
`configMAX_PRIORITIES` 默认只有 25 级，所以映射**一定是有损的**。

本层的取舍是：**保证 8 个主档精确往返**

```
osPriorityIdle / osPriorityLow / osPriorityBelowNormal / osPriorityNormal
osPriorityAboveNormal / osPriorityHigh / osPriorityRealtime / osPriorityISR
```

即 `osThreadNew(..., osPriorityNormal)` 后用 `osThreadGetPriority()` 读回来的值
和当初设进去的**完全相同**。主档之间的细分档（如 `osPriorityNormal1` ~ `Normal7`）
会被压缩，不保证一一对应。

> 如果你确实需要 56 级都能分辨：ESP-IDF 的 `FreeRTOSConfig.h` 是私有的，不能在
> 工程里改。降低对细分档的依赖是更现实的方案。

**切勿**把 FreeRTOS 的 `uxPriority` 数值直接当作参数传给 `os*` 函数。

---

## 三、支持的 API

全部按 CMSIS-RTOS2 v2 语义实现：

| 分组 | 函数 |
|---|---|
| 内核 | `osKernelInitialize` `osKernelGetInfo` `osKernelGetState` `osKernelStart` `osKernelLock` `osKernelUnlock` `osKernelRestoreLock` `osKernelSuspend` `osKernelResume` `osKernelGetTickCount` `osKernelGetTickFreq` `osKernelGetSysTimerCount` `osKernelGetSysTimerFreq` |
| 线程 | `osThreadNew` `osThreadGetName` `osThreadGetId` `osThreadGetState` `osThreadSetPriority` `osThreadGetPriority` `osThreadYield` `osThreadSuspend` `osThreadResume` `osThreadDetach` `osThreadJoin` `osThreadExit` `osThreadTerminate` `osThreadGetStackSize` `osThreadGetStackSpace` `osThreadGetCount` `osThreadEnumerate` |
| 线程标志 | `osThreadFlagsSet` `osThreadFlagsClear` `osThreadFlagsGet` `osThreadFlagsWait` |
| 延迟 | `osDelay` `osDelayUntil` |
| 定时器 | `osTimerNew` `osTimerGetName` `osTimerStart` `osTimerStop` `osTimerIsRunning` `osTimerDelete` |
| 事件标志 | `osEventFlagsNew` `osEventFlagsSet` `osEventFlagsClear` `osEventFlagsGet` `osEventFlagsWait` `osEventFlagsDelete` |
| 互斥量 | `osMutexNew` `osMutexAcquire` `osMutexRelease` `osMutexGetOwner` `osMutexDelete` |
| 信号量 | `osSemaphoreNew` `osSemaphoreAcquire` `osSemaphoreRelease` `osSemaphoreGetCount` `osSemaphoreDelete` |
| 内存池 | `osMemoryPoolNew` `osMemoryPoolAlloc` `osMemoryPoolFree` `osMemoryPoolGetCapacity` `osMemoryPoolGetBlockSize` `osMemoryPoolGetCount` `osMemoryPoolGetSpace` `osMemoryPoolDelete` |
| 消息队列 | `osMessageQueueNew` `osMessageQueuePut` `osMessageQueueGet` `osMessageQueueGetCapacity` `osMessageQueueGetMsgSize` `osMessageQueueGetCount` `osMessageQueueGetSpace` `osMessageQueueReset` `osMessageQueueDelete` |

### ESP32 专有扩展（`esp_cmsis_os2_ext.h`，可选）

| 函数 | 说明 |
|---|---|
| `osThreadNewCore(func, arg, attr, core)` | 创建任务并指定核心：`OS2_CORE_PRO_CPU`(0) / `OS2_CORE_APP_CPU`(1) / `OS2_CORE_NO_AFFINITY` |
| `osThreadSetCoreAffinity(id, mask)` | 运行时修改任务的核心亲和性 |
| `osThreadGetCoreId()` | 查询当前任务跑在哪个核（0/1） |

---

## 四、已知限制

1. **不支持 `osThreadJoin`**：FreeRTOS 没有 join 语义，`osThreadJoin` 返回 `osError`，
   带 `osThreadJoinable` 属性的 `osThreadNew` 也会直接返回 `NULL`（ARM 官方 CMSIS-FreeRTOS 同样如此）。
2. **消息队列无优先级**：FreeRTOS 的队列不支持同一队列内消息优先级，
   `osMessageQueuePut` 的 `msg_prio` 参数只能区分「队首/队尾」——`0` 入队尾，`>0` 入队首。
3. **静态创建门槛**：只有 `attr->cb_mem` 与 `attr->stack_mem` **同时**提供时才走静态路径
   （`xTaskCreateStaticPinnedToCore`）；否则走动态路径（`xTaskCreatePinnedToCore`），
   由 FreeRTOS 堆托管内存、任务删除后由 Idle 自动回收，无泄漏。
4. **优先级有损映射**：见上文，主档精确往返，细分档压缩。
5. **`osKernelGetSysTimerCount/Freq`**：ESP32 上基于 `esp_timer_get_time()`，返回微秒级计数，频率固定 1 MHz。

---

## 五、验证情况

本组件用**真实的 FreeRTOS 内核 v11.1.0**（POSIX/GCC port）在 Linux 上编译并通过完整回归测试：

```
PASS: 83     FAIL: 0
>>> ALL TESTS PASSED <<<
```

覆盖：队列值拷贝语义、事件/线程标志的 Any/All/NoClear 组合与「部分位不丢失」、
内存池块不重叠、周期定时器回调、优先级主档精确往返等。ESP_PLATFORM 分支也单独做了语法检查。

过程中这套代码自己还被测试抓出并修掉了三个真实缺陷：
`osKernelStart` 重复启动会挂死、软件定时器回调签名不兼容、优先级 set/get 往返不一致。

---

## 六、License

Apache-2.0，派生自 [ARM-software/CMSIS-FreeRTOS](https://github.com/ARM-software/CMSIS-FreeRTOS)。

# ESP-IDF 组件注册
#
# cmsis_os2 —— 让 ESP32 上可以继续使用 CMSIS-RTOS2 (os*) API
idf_component_register(
    SRCS
        "src/cmsis_os2.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
    PRIV_REQUIRES
        esp_timer
)

# CMSIS-RTOS2 大量依赖 Task/Queue/Semphr 的底层 API；
# 栈水位查询依赖 INCLUDE_uxTaskGetStackHighWaterMark，
# 但即使未开启也能正常编译（代码里已做条件编译降级）。
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)

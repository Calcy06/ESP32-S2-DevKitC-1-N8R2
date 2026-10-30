# main 组件 CMakeLists.txt
#
# 【依赖怎么写 —— 这是最容易踩坑的地方】
#
# ESP-IDF v5.2 起把一个大组件 driver 拆成了若干小组件：
#     esp_driver_gpio      → driver/gpio.h
#     esp_driver_i2c       → driver/i2c_master.h、driver/i2c.h
#     esp_driver_uart、esp_driver_spi、esp_driver_ledc ...
# 你用的是 v6.0.2，必须写拆分后的名字。只写 REQUIRES driver 的话，
# 编译会报：
#     fatal error: driver/gpio.h: No such file or directory
#     esp_driver_gpio component(s) is not in the requirements list of "main"
#
# 而 v5.1 及更早版本还没有这些拆分后的组件名，写了反而会报
# "component not found"。下面这段 if 让这份 CMakeLists 两种版本都能用。
# ---------------------------------------------------------------------------
if(EXISTS "${IDF_PATH}/components/esp_driver_i2c")
    # IDF >= v5.2（含你用的 v6.0.2）：用拆分后的组件名
    set(DRIVER_DEPS esp_driver_i2c esp_driver_gpio)
else()
    # IDF <= v5.1：driver 还是一整个
    set(DRIVER_DEPS driver)
endif()

message(STATUS "oled: using driver components: ${DRIVER_DEPS}")

idf_component_register(
    SRCS "main.c" "ssd1306.c"
    INCLUDE_DIRS "."
    REQUIRES ${DRIVER_DEPS}
)
#
# 说明：这些都是 IDF 自带组件，不需要 add-dependency、也不需要联网下载。
# 本工程没有引入任何外部组件，SSD1306 驱动和字体都是自己写的。
#
# 【如果以后想引入远程组件】见 README 第 3 节：
#   idf.py add-dependency "命名空间/组件名"   → 生成 main/idf_component.yml
#   首次 build 时自动下载到 managed_components/，无需改这里的 REQUIRES。

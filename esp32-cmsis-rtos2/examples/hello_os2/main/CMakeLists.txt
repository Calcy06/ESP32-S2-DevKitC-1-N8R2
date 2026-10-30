# 示例工程的主组件
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES cmsis_os2 esp_driver_gpio
)

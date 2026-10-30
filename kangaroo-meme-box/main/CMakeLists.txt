idf_component_register(
    SRCS
        "main.c"
        "gfx.c"          # 纯软件图形库（不依赖 IDF，可在 PC 上预览）
        "ssd1306.c"      # OLED 的 I2C 传输层
        "kangaroo.c"     # 袋鼠形象绘制
        "buzzer.c"       # 有源蜂鸣器节奏驱动
    INCLUDE_DIRS "."
    REQUIRES
        # IDF v5.3+ 把原来的 driver 拆成了多个 esp_driver_* 组件，
        # v6.x 里再写 driver 会拿不到 driver/gpio.h、driver/i2c.h 的头文件路径
        esp_driver_gpio  # driver/gpio.h
        esp_driver_i2c  # driver/i2c.h、driver/i2c_master.h
        esp_timer       # esp_timer.h / esp_timer_get_time()
        esp_hw_support  # esp_random.h
)

# 汉字点阵 + ASCII 点阵都是 const，放在 Flash 里
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)

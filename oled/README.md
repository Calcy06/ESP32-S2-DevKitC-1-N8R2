# ESP32-S2 + SSD1306 OLED 显示一行文本（零外部组件版）

硬件：**ESP32-S2-DevKitC-1-N8R2**（ESP32-S2-SOLO-2 模组，8 MB Flash + 2 MB PSRAM）
屏幕：**SSD1306 驱动的 0.96 寸 OLED，128x64，I2C 接口**（就是最常见的 4 针模块）

本工程**不含 `Kconfig.projbuild`、不含 `sdkconfig.defaults`**，也没有引入任何外部组件。
SSD1306 驱动和 5x7 字体都是自己写的，不联网也能编译。

```
oled/
├── CMakeLists.txt            # 顶层工程入口
├── README.md
└── main/
    ├── CMakeLists.txt        # REQUIRES esp_driver_i2c esp_driver_gpio（IDF 内置，见第 3 节）
    ├── font5x7.h             # 5x7 点阵字体，ASCII 32~126
    ├── ssd1306.c / ssd1306.h # 自写的 SSD1306 I2C 驱动 + 画点/画字
    └── main.c                # 参数 #define + app_main
```

---

## 1. 接线（ESP32-S2-DevKitC-1 ↔ SSD1306）

只有 4 根线：

| SSD1306 模块 | ESP32-S2-DevKitC-1 | 排针位置 | 说明 |
|---|---|---|---|
| **VCC** | **3V3** | J3 第 1 或 2 脚 | 模块一般 3.3~5V 通用，接 3V3 最省事（电平也匹配） |
| **GND** | **G** | J3 第 22 脚（或 J1 第 1/21 脚） | 必须共地 |
| **SDA** | **GPIO8** | J3 第 12 脚 | 本工程默认 |
| **SCL** | **GPIO9** | J3 第 15 脚 | 本工程默认 |

```
        SSD1306 (4-pin)          ESP32-S2-DevKitC-1
      ┌──────────────┐
      │ VCC       GND│             3V3  ●─────────────● VCC
      │              │               G   ●─────────────● GND
      │ SDA       SCL│          GPIO8    ●─────────────● SDA
      └──────────────┘          GPIO9    ●─────────────● SCL
```

**为什么选 GPIO8 / GPIO9**：ESP32-S2 的 I2C 可以通过 GPIO 矩阵映射到几乎任意引脚，
这两个脚不在任何"黑名单"上（非 strapping、非 JTAG、非 flash/PSRAM、非 USB、非 UART0），
而且 DevKitC-1 两根排针上都引出来了。**想换脚改 `main.c` 里的两个宏就行。**

**为什么不推荐这些脚**（和蜂鸣器工程同一套规则）：

| 引脚 | 原因 |
|---|---|
| GPIO26~32 | 内部 flash/PSRAM 占用（DevKitC-1 上 27~32 根本没引出） |
| GPIO0 / GPIO45 / GPIO46 | Strapping 脚；GPIO46 还是**仅输入** |
| GPIO39~42 | JTAG 调试口 |
| GPIO43 / GPIO44 | UART0，烧录和日志要用 |
| GPIO19 / GPIO20 | 原生 USB OTG 的 D− / D+ |
| GPIO18 | **这块板的板载 RGB LED 接在这里**，占了会一直亮 |

> **关于上拉电阻**：SSD1306 模块上通常自带 4.7kΩ 上拉，短线（<20cm）不用外加。
> 代码里还开了 ESP32-S2 的内部上拉（`flags.enable_internal_pullup = true`）作为双保险。

---

## 2. 从零开始：一步步跑通

```bash
# Step 0 —— 导出 IDF 环境（每个新终端都要）
. $IDF_PATH/export.sh

# Step 1 —— 把 oled 文件夹拷到虚拟机，例如 ~/test/oled
cd ~/test/oled

# Step 2 —— ★ 指定芯片型号（本工程没有 sdkconfig.defaults，这步必须手动做）
idf.py set-target esp32s2
grep CONFIG_IDF_TARGET sdkconfig        # 应显示 "esp32s2"

# Step 3 —— 自己配框架参数
idf.py menuconfig

# Step 4 —— 改代码里的引脚/文本（见 2.2）

# Step 5 —— 编译烧录
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor    # 退出：Ctrl + ]
```

### 2.2 Step 3 在 menuconfig 里要改什么

这台板子 Flash 是 **8 MB**，而 IDF 默认通常不是 8 MB，建议改成一致：

```
按 / 搜 ESPTOOLPY_FLASHSIZE  →  回车  →  选 8 MB
（路径：Serial flasher config → Flash size）
```

改完按 `S` 保存、`Q` 退出。**其余选项保持默认即可**，本工程不需要动别的。

> 顺带一提：N8R2 的 2 MB PSRAM 本工程用不上，不开也能正常跑。
> 想用就搜 `SPIRAM` 打开（`Component config → ESP PSRAM`）。

按键速查：`↑↓` 移动 · `Enter` 进入 · `空格` 勾选 · **`/` 搜索** · `?` 帮助 · **`S` 保存** · **`Q` 退出**。

### 2.3 Step 4 改代码

打开 `main/main.c` 顶部：

```c
#define OLED_SDA_GPIO   GPIO_NUM_8   /* SDA 引脚 */
#define OLED_SCL_GPIO   GPIO_NUM_9   /* SCL 引脚 */
#define OLED_I2C_ADDR   0x3C         /* 屏幕不亮就试 0x3D */
#define OLED_WIDTH      128
#define OLED_HEIGHT     64           /* 128x32 的屏这里填 32 */
#define OLED_TEXT       "Hello ESP32-S2!"
```

改完保存即可，**不用再动 menuconfig**。

---

## 3. 组件：SSD1306 到底要不要引入？

**不用。** 这正是本工程的设计选择。先把 ESP-IDF 的三种组件来源说清楚：

| 来源 | 怎么用 | 要不要联网 |
|---|---|---|
| **① IDF 内置组件**<br>（`esp_driver_i2c`、`esp_driver_gpio`、`esp_lcd`、`esp_wifi`…） | 在 `main/CMakeLists.txt` 里 `REQUIRES xxx` 声明 | **不用**，IDF 自带 |
| **② 组件注册表的远程组件** | `idf.py add-dependency "命名空间/组件名"` | **要**，首次 build 自动下载 |
| **③ 本地组件** | 新建 `components/组件名/` 目录，放 `CMakeLists.txt` + 源码 | 不用 |

本工程只用了 ①。SSD1306 的通信协议非常简单——「I2C 写一个控制字节 + 写命令或显存数据」，
自己写 200 行就能显示文字，**没必要为此联网拉一个组件**。

### 3.0 ⚠️ IDF v5.2+ 已把 `driver` 拆开，`REQUIRES driver` 会编译失败

这是本工程实测踩到的坑（v6.0.2 上必现）。从 **v5.2** 起，原本一个大组件 `driver`
被拆成了一批小组件：

| 头文件 | v5.1 及更早 | **v5.2+ / v6.x（你用的版本）** |
|---|---|---|
| `driver/gpio.h` | `driver` | **`esp_driver_gpio`** |
| `driver/i2c_master.h`、`driver/i2c.h` | `driver` | **`esp_driver_i2c`** |
| `driver/uart.h` | `driver` | `esp_driver_uart` |
| `driver/spi_master.h` | `driver` | `esp_driver_spi` |
| `driver/ledc.h` | `driver` | `esp_driver_ledc` |

只写 `REQUIRES driver` 时的**典型报错**（就是这次遇到的）：

```
fatal error: driver/gpio.h: No such file or directory
Compilation failed because ssd1306.h (in "main" component) includes driver/gpio.h,
provided by esp_driver_gpio component(s).
However, esp_driver_gpio component(s) is not in the requirements list of "main".
To fix this, add esp_driver_gpio to REQUIRES list of idf_component_register call
in .../main/CMakeLists.txt.
```

好消息是 **IDF 的错误信息会直接告诉你该加哪个组件名**——看到
`provided by xxx component(s)` 就把 `xxx` 加进 `REQUIRES` 即可，不用猜。

本工程的 `main/CMakeLists.txt` 已写成按版本自动选择，两种 IDF 都能编：

```cmake
if(EXISTS "${IDF_PATH}/components/esp_driver_i2c")
    set(DRIVER_DEPS esp_driver_i2c esp_driver_gpio)   # IDF >= v5.2
else()
    set(DRIVER_DEPS driver)                          # IDF <= v5.1
endif()

idf_component_register(
    SRCS "main.c" "ssd1306.c"
    INCLUDE_DIRS "."
    REQUIRES ${DRIVER_DEPS}
)
```

> 💡 通用规律：凡是 `driver/xxx.h` 找不到，先按上表换成对应的 `esp_driver_xxx`。

### 3.1 如果想用远程组件（方法二，学一下备用）

```bash
cd ~/test/oled
idf.py add-dependency "lvgl/lvgl"        # 举例：图形库
```

这条命令会生成 `main/idf_component.yml`：

```yaml
dependencies:
  lvgl/lvgl: "^9.2.0"
```

首次 `idf.py build` 时自动下载到 `managed_components/`，**一般不用改 CMakeLists**
（组件自己的 CMakeLists 会声明依赖关系）。想去掉就删掉 yml 里那一行 + 删 `managed_components/`。

> ⚠️ 联网下载受虚拟机网络影响，这也是我推荐零依赖写法的原因之一。

### 3.2 如果想用官方的 esp_lcd 驱动 SSD1306（方法一）

ESP-IDF **内置** `esp_lcd` 组件，里面就有 SSD1306 面板驱动，不用下载：

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio   # v5.2+ 写法
)
```

```c
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"

/* 建 I2C 总线（和本工程一样） */
i2c_master_bus_handle_t bus;
i2c_master_bus_config_t bus_cfg = {
    .i2c_port = I2C_NUM_0, .sda_io_num = 8, .scl_io_num = 9,
    .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
i2c_new_master_bus(&bus_cfg, &bus);

/* 把 SSD1306 挂成 I2C 设备 */
esp_lcd_panel_io_handle_t io;
esp_lcd_panel_io_i2c_config_t io_cfg = {
    .dev_addr = 0x3C, .scl_speed_hz = 400000,
    .control_phase_bytes = 1, .dc_bit_offset = 6,
    .lcd_cmd_bits = 8, .lcd_param_bits = 8,
};
esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io);

/* 创建 SSD1306 面板 */
esp_lcd_panel_ssd1306_config_t ssd_cfg = { .height = 64 };   /* ★ 高版本 IDF 要填 */
esp_lcd_panel_dev_config_t panel_cfg = {
    .bits_per_pixel = 1, .reset_gpio_num = -1, .vendor_config = &ssd_cfg,
};
esp_lcd_panel_handle_t panel;
esp_lcd_new_panel_ssd1306(io, &panel_cfg, &panel);

esp_lcd_panel_reset(panel);
esp_lcd_panel_init(panel);
esp_lcd_panel_disp_on_off(panel, true);
```

**但有两点要注意**：

1. `esp_lcd` **只负责把像素推到屏幕**，它**不含字体**。要显示文字还是得自己准备字模，
   或者再引入 LVGL —— 那就要联网 + 多学一套 API。
2. 不同 IDF 版本的 `esp_lcd_panel_ssd1306` 配置结构有变化（v5.1 前后是否需要
   `vendor_config` / `.height` 不一样）。这也是我给你自写驱动的原因：**不受版本影响**。

---

## 4. 验证结果

屏幕中央显示一行 `Hello ESP32-S2!`（水平垂直居中），串口日志：

```
I (321) ssd1306: SSD1306 ready: 128x64, I2C addr 0x3C, SDA=GPIO8 SCL=GPIO9
I (331) oled_demo: display updated: "Hello ESP32-S2!" at (19, 28)
```

只有日志成功、屏幕不亮 → 看第 5 节。

---

## 5. 常见问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 屏幕完全不亮 | I2C 地址不对 | 把 `OLED_I2C_ADDR` 改成 `0x3D` 试试（两种都有卖） |
| 屏幕完全不亮 | SDA / SCL 接反或接错脚 | 对照第 1 节重接；用万用表量 VCC 是否有 3.3V |
| 屏幕全黑但背光/通电 | 初始化序列里电荷泵没开 | 本工程 `0x8D 0x14` 已开；自己改代码时注意别删 |
| 屏幕全黑 | 忘了调 `ssd1306_refresh()` | 画完必须刷新，显存在 MCU 内存里不会自动同步 |
| 只显示一半 / 上下颠倒 | 屏幕其实是 128x32 | `OLED_HEIGHT` 改成 32（初始化命令会跟着变） |
| `i2c_new_master_bus failed` | 引脚被占用或非法 | 换 GPIO8/9；别用 26~32、46 等禁用脚 |
| 文字显示成乱码方块 | 字符超出 ASCII 32~126 | 字库只覆盖 ASCII；中文需要自己做字模 |
| 编译报 `driver/gpio.h: No such file or directory` + `esp_driver_gpio ... is not in the requirements list` | IDF v5.2+ 把 `driver` 拆分了 | 见 **第 3.0 节**：`REQUIRES` 改成 `esp_driver_i2c esp_driver_gpio` |
| 编译报 `driver/i2c_master.h` 找不到 | 同上，I2C 也拆出来了 | 加 `esp_driver_i2c` |
| 烧录后反复重启 | 供电不足 | OLED 电流很小，一般是 USB 线或口的问题，换一根 |

**注意**：OLED 是 3.3V 器件，**不要**把 VCC 接到 5V 再和 ESP32-S2 的 IO 直连
（虽然多数模块自带稳压允许 5V 供电，但 SDA/SCL 仍是 3.3V 电平，接 5V 供电时务必确认模块带电平转换）。

---

## 6. 想再进一步

- **多行文本**：多调几次 `ssd1306_draw_string()`，`y` 每次加 8~10。
- **动态内容**：`ssd1306_clear()` → 重画 → `ssd1306_refresh()`，放进 `while(1)` + `vTaskDelay()`。
- **画图形**：`ssd1306_draw_pixel()` 已经有了，画线/画框在它之上封装即可。
- **换字体**：替换 `font5x7.h`，同步改 `FONT5X7_WIDTH` / `FONT5X7_HEIGHT`
  和 `ssd1306_draw_char()` 里的绘制循环。
- **中文显示**：需要 16x16 点阵字模（每个汉字 32 字节），用取模软件生成后加进字体表。

常用命令：

```bash
idf.py fullclean                 # 彻底清理（换芯片型号时必须）
idf.py reconfigure               # 只重跑配置
idf.py -p /dev/ttyUSB0 monitor   # 只监视不烧录
idf.py size                      # 看固件体积
```

# 袋鼠整活盒 · ESP32-S2 移植版

把「圆滚滚黄色袋鼠 + 你胆子真是肥嘟嘟的」这套整活搬到 **ESP32-S2-DevKitC-1-N8R2 + SSD1306 OLED + 有源蜂鸣器** 上：
OLED 播袋鼠动画和滚动字幕，蜂鸣器按中文语流打出「台词节奏」，按一下 BOOT 键就换一句。

整机只用到 **两块外设**，没有任何额外传感器。

---

## 1. 硬件与接线

### 1.1 引脚分配

| 外设 | 信号 | ESP32-S2 引脚 | 说明 |
|------|------|---------------|------|
| SSD1306 OLED | SDA | **GPIO8** | I2C 数据，任意 GPIO 均可，可在 menuconfig 改 |
| SSD1306 OLED | SCL | **GPIO9** | I2C 时钟 |
| SSD1306 OLED | VCC / GND | 3V3 / GND | 模块自带 4.7k 上拉时可不外加上拉电阻 |
| 有源蜂鸣器 | 信号端 | **GPIO10** | 高电平 = 响 |
| 触发按键 | 一端 | **GPIO0** | 直接用板载 BOOT 键，按下为低；也可以外接按键到 GND |
| — | — | GPIO26~32 | ⚠️ 被内部 Flash/PSRAM 占用，**不可用** |
| — | — | GPIO45/46 | ⚠️ strapping 引脚，别拿来驱动外设 |

### 1.2 OLED 接线

```
SSD1306          ESP32-S2-DevKitC-1
 VCC  -----------  3V3
 GND  -----------  GND
 SDA  -----------  GPIO8
 SCL  -----------  GPIO9
```

模块背面的 `0x78 / 0x7A` 焊盘决定地址：默认 **0x3C**，跳到另一边是 0x3D。
改地址用 `idf.py menuconfig → 袋鼠整活盒 → OLED I2C 地址`。

### 1.3 蜂鸣器接线（重点）

有源蜂鸣器工作电流一般 20~30mA，ESP32-S2 单引脚长期建议不超过 ~20mA，
**推荐用一只 NPN 三极管或 MOSFET 做开关**，别让 GPIO 硬扛：

```
                 +3V3 (或 5V)
                   │
                 ┌─┴─┐
                 │蜂鸣器│  (有源，高电平触发)
                 └─┬─┘
                   │  C
        GPIO10 ─1k─┤ B   NPN (如 S8050 / 2N3904)
                   │  E
                  GND
```

逻辑不变：**GPIO 输出高电平 → 三极管导通 → 蜂鸣器响**。
若你的蜂鸣器电流很小（<15mA）且只想临时验证，可以直接 GPIO10 → 蜂鸣器+，蜂鸣器- → GND，
但长时间运行建议还是加三极管，并在蜂鸣器两端并联一只反向续流二极管。

---

## 2. 快速开始（Linux 虚拟机）

```bash
# 0) 把开发板的 USB 串口透传到虚拟机（VMware/VBox 里勾选 USB 设备）
ls /dev/ttyUSB* /dev/ttyACM*        # 出现 ttyUSB0 说明识别到了
sudo usermod -aG dialout $USER      # 加串口组，注销重登生效

# 1) 进入工程并指定目标芯片（只需做一次，会生成 sdkconfig）
cd kangaroo-meme-box
idf.py set-target esp32s2

# 2) 按需改引脚 + 选控制台通道
idf.py menuconfig                   # → 袋鼠整活盒 (Kangaroo Meme Box)
                                    # 逐项说明见 docs/menuconfig.md

# 3) 编译
idf.py build

# 4) 烧录 + 看日志（menuconfig 里控制台选的是 UART0）
idf.py -p /dev/ttyUSB0 -b 460800 flash monitor

# 若选的是 USB Serial/JTAG，则插标 "USB" 的口，用：
# idf.py -p /dev/ttyACM0 flash monitor

# 退出 monitor：Ctrl+]
```

> ESP32-S2-DevKitC-1 上板载 USB 转串口芯片（CP210x/FTDI），可直接用串口烧录。
> 如果烧录失败提示 `Failed to connect`，按住 BOOT 键再按一下 EN(RST) 进入下载模式重试。

---

## 3. 从 STM32 迁到 ESP32-S2，关键点都在这

原视频里的项目是 STM32 那一套，换到 ESP32-S2 主要踩这些坑：

| 事项 | STM32（原项目） | ESP32-S2（本工程） |
|------|-----------------|--------------------|
| 内核 | ARM Cortex-M，裸机/HAL | Xtensa LX7 **单核** 240MHz，跑 FreeRTOS |
| 构建 | Keil / CubeMX / Makefile | **CMake + ESP-IDF 组件** |
| GPIO 命名 | `GPIOA->ODR`、`HAL_GPIO_WritePin(GPIOA, PIN_x)` | 统一编号 `gpio_set_level(GPIO_NUM_10, 1)`，无端口概念 |
| 引脚复用 | 每个外设绑定固定引脚 | **几乎任意 GPIO 可重映射**（除 26~32、45/46） |
| I2C | `HAL_I2C_Transmit` / 软件模拟 | `i2c_driver_install()` + 命令链路；本工程封装在 `ssd1306.c` |
| 延时 | `HAL_Delay(ms)`（阻塞） | `vTaskDelay(pdMS_TO_TICKS(ms))`，配合 `esp_timer_get_time()` 做非阻塞调度 |
| 定时器 | `TIM2` 中断 | `esp_timer` / `gptimer`；蜂鸣节奏用 `esp_timer_get_time()` 状态机实现，不占硬件定时器 |
| 随机数 | `rand()` | `esp_random()`（真随机源） |
| 烧录调试 | ST-Link / SWD | 串口 bootloder 或 USB Serial/JTAG，`idf.py flash monitor` |
| 无线 | 若原项目用了蓝牙模块 | ⚠️ **ESP32-S2 没有蓝牙**，只有 Wi-Fi（N8R2 也没引出以太网） |
| 蜂鸣器调音 | 可 PWM 变频（无源） | ⚠️ **有源蜂鸣器音调固定**，只能控制"响多久/停多久"，所以本工程玩的是节奏而不是旋律 |

### 3.1 有源蜂鸣器的两条硬约束

1. **音调改不了**：内部自带振荡器，通电就是固定频率。想有旋律必须换**无源**蜂鸣器再用 LEDC 输出 PWM。
2. **音量改不了**：只有开/关。本工程因此把信息量全压在"时长组合"上——见 `buzzer.c` 里的台词节奏表。

### 3.2 ESP32-S2 的三个专属坑

- **GPIO26~32 不能动**：板载 8MB Flash + 2MB PSRAM 走的就是这几个脚。
- **USB 与 GPIO19/20**：S2 有原生 USB-OTG（占 GPIO19/20），本工程没用它，但如果你后续要加 USB 功能，别把 OLED 挂到 19/20。
- **PSRAM 默认未启用**：本工程 1KB 帧缓冲够用，所以 `sdkconfig.defaults` 里没开 PSRAM；要用大缓冲（比如整屏图片轮播）再打开 `CONFIG_ESP32S2_SPIRAM_SUPPORT`。

---

## 4. 目录结构

```
kangaroo-meme-box/
├── CMakeLists.txt            # 工程根
├── sdkconfig.defaults         # ESP32-S2 / 8MB Flash / 240MHz 默认配置
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild      # menuconfig 里的"袋鼠整活盒"菜单
│   ├── board_config.h         # 引脚默认值（menuconfig 未配置时的兜底）
│   ├── main.c                 # 状态机：开机→待机→台词→待机，含按键与字幕滚动
│   ├── gfx.c / gfx.h          # 纯软件 1bpp 图形库（点/线/圆/椭圆/圆角矩形/文本）
│   ├── ssd1306.c / .h         # OLED 的 I2C 传输层（IDF v4.4~v5.x 与 v6 双分支）
│   ├── kangaroo.c / .h        # 袋鼠形象：全部由椭圆拼出来，不占 Flash 存图
│   ├── buzzer.c / .h          # 有源蜂鸣器非阻塞节奏引擎 + 台词节奏表
│   ├── font_cn.h              # 16x16 汉字点阵（脚本生成，共 23 字 / 736B）
│   └── font_ascii.h           # 5x7 ASCII 点阵（源自 Adafruit GFX，BSD）
├── tools/
│   ├── gen_font.py            # 重新生成汉字点阵（改台词后要跑）
│   ├── gen_ascii.py           # 从 glcdfont.c 生成 ASCII 点阵
│   ├── glcdfont.c             # Adafruit 5x7 原始字模（gen_ascii.py 的输入）
│   ├── preview_host.c         # 只画静态帧：验证排版/造型
│   ├── sim.c                  # 软件在环仿真：把整个固件逻辑在 PC 上跑一遍
│   └── esp_stubs/             # PC 端用的 IDF 头文件桩
└── docs/
    ├── wiring.md              # 接线图与常见接线错误
    ├── menuconfig.md          # menuconfig 每一步怎么配（含 S2 双 USB 口选型）
    ├── preview.gif            # 仿真跑出来的动画效果
    └── preview_frames.png     # 四个关键状态截图
```

> 第一次上手直接看 **[docs/menuconfig.md](docs/menuconfig.md)**，
> 里面有 menuconfig 的逐项说明和 ESP32-S2 控制台通道的选择方法。

---

## 5. 改成你自己的梗

### 5.1 换台词

编辑 `main/main.c` 里的 `LINES[]`：

```c
static const line_t LINES[] = {
    { .text = "你胆子真是肥嘟嘟的", .notes = BUZZ_LINE_SMUG,  .notes_len = 9, .mood = MOOD_SMUG,  .hold_box = true  },
    { .text = "敢抢我的外卖",       .notes = BUZZ_LINE_ANGRY, .notes_len = 5, .mood = MOOD_ANGRY, .hold_box = true  },
    { .text = "单片机可真好玩",     .notes = BUZZ_LINE_FUN,   .notes_len = 7, .mood = MOOD_TALK,  .hold_box = false },
};
```

**注意**：汉字点阵只包含已有字符（你 胆 子 真 是 肥 嘟 的 敢 抢 我 外 卖 不 打 听 谁 单 片 机 可 好 玩）。
新增台词要重新生成字库：

```bash
cd tools
python3 gen_font.py "你胆子真是肥嘟嘟的敢抢我的外卖不打听谁单片机可好玩这里写你的新字"
```

缺字会在屏幕上显示一个空心方框，一眼能看出来。

### 5.2 调节奏

在 `main/buzzer.c` 里改 `BUZZ_LINE_*` 数组，`on_ms` 是响多久、`off_ms` 是停多久。
节奏遵循中文语流：**短促的字给 60~80ms，收尾的字拉到 140ms 左右**，"顿挫感"就出来了。

### 5.3 改袋鼠长相

`kangaroo.c` 里全是椭圆坐标，改数字即可，不用重新导出图片：

```c
gfx_fill_ellipse(X(0), Y(4), L(11 + sq), L(9 - sq), COL_WHITE);   /* 身体：改这两个半径就是胖瘦 */
gfx_fill_ellipse(X(hx), Y(hy), L(9), L(8), COL_WHITE);            /* 头 */
gfx_fill_ellipse(X(hx - 5), Y(hy - 6), L(3), L(7), COL_WHITE);    /* 耳朵：ry 越大耳朵越长 */
```

### 5.4 上板前先在 PC 上看效果

这套代码刻意把「图形库」和「ESP-IDF 传输层」拆开了，所以**不接板子、不装工具链**也能验证：

**方式一：只看静态造型**

```bash
cd tools
gcc -O2 -I../main preview_host.c ../main/gfx.c ../main/kangaroo.c -lm -o preview
./preview out 4            # 生成 out_1boot.pbm ~ out_4angry.pbm，4 = 放大倍数
```

**方式二：跑完整固件逻辑（推荐）**

`sim.c` 用桩把 `gpio / i2c / esp_timer / FreeRTOS` 全部实现了一遍，
然后把 `app_main()` 原封不动跑起来，输出 500 帧、每帧 1024 字节的 `frames.bin`：

```bash
cd tools
gcc -O2 -I../main -Iesp_stubs sim.c ../main/*.c -lm -o sim
./sim
# [I][meme-box] 装好的日志会一行行打出来，包括每一次按键、每一句台词
# 仿真结束
# 总帧数 : 500 (20.0 秒 / 25 fps)
```

它会顺带验证这些事：

- 开机动画 → 待机 → 台词 → 待机 的状态流转
- 第 60 帧模拟一次短按（120ms）→ 是否切到下一句
- 第 200~235 帧模拟一次长按（1400ms）→ 是否触发静音
- 蜂鸣器引脚被拉高的累计时长（节奏对不对）

把 `frames.bin` 转成图片或 GIF：

```bash
python3 - <<'EOF'
from PIL import Image
data = open('frames.bin','rb').read()
def to_img(fb):
    im = Image.new('1',(128,64),0); px = im.load()
    for page in range(8):
        for x in range(128):
            b = fb[page*128+x]
            for bit in range(8):
                if (b>>bit)&1: px[x, page*8+bit] = 1
    return im
frames = [to_img(data[i*1024:(i+1)*1024]).resize((256,128), Image.NEAREST).convert('P')
          for i in range(0, len(data)//1024, 2)]
frames[0].save('demo.gif', save_all=True, append_images=frames[1:], duration=80, loop=0)
EOF
```

> 本工程 `docs/preview.gif` 就是这么生成的——也就是说，你拿到的效果图是**真实固件逻辑跑出来的**，不是画的示意图。

---

## 6. 交互说明

| 操作 | 效果 |
|------|------|
| 短按 BOOT 键（GPIO0） | 播放下一句台词（循环切换） |
| 长按 BOOT 键 1.2 秒 | 静音 / 取消静音（右上角出现划掉的喇叭图标） |
| 放着不管 | 每 10 秒自动来一句（可在 menuconfig 改间隔或关闭） |

---

## 7. 排错

| 现象 | 原因 / 处理 |
|------|-------------|
| 日志出现 `first flush failed` 或 `init cmd failed` | I2C 没通：查 SDA/SCL 是否接反、地址是不是 0x3D、有没有上拉、VCC 是否 3.3V |
| 屏幕全黑但日志正常 | 对比度/电荷泵命令没生效，或模块不是 SSD1306（SH1106 需要改列偏移，见 `ssd1306.c` 注释） |
| 字幕位置出现空心方框 | 该汉字不在字库里，跑 `gen_font.py` 补字 |
| 蜂鸣器不响 | 高电平触发确认：直接 `gpio_set_level(GPIO_NUM_10, 1)` 测一下；三极管方向有没有焊反 |
| 蜂鸣器一直响 | 检查三极管是否击穿短路，或 GPIO 上拉/下拉配置 |
| 动画卡、一顿一顿 | I2C 400kHz 传 1024 字节约 23ms，把 `FRAME_INTERVAL_MS` 调到 50，或把 `ssd1306_init` 里的 `clk_speed` 提到 800kHz |
| `idf.py: command not found` | 没 source 环境：`. $IDF_PATH/export.sh` |
| 虚拟机里 `/dev/ttyUSB0` 不存在 | 在虚拟机软件里把 USB 串口设备透传给虚拟机，并确认 `dialout` 组已生效 |

---

## 8. 验证状态（拿到手先看这段）

| 项目 | 状态 | 说明 |
|------|------|------|
| PC 端语法检查 | ✅ 通过 | `gcc -Wall -Wextra` 零警告，覆盖 `main.c / gfx.c / ssd1306.c / kangaroo.c / buzzer.c` |
| 软件在环仿真 | ✅ 通过 | `tools/sim.c` 跑满 500 帧，状态机流转、短按换句、长按静音、蜂鸣时序全部符合预期 |
| 渲染效果 | ✅ 已验证 | `docs/preview.gif` 就是仿真输出的真实帧序列 |
| 真实 IDF 编译 | ⚠️ 未验证 | 制作环境拉不下完整 IDF 工具链，需要你在本机跑一次 `idf.py build` |

如果 `idf.py build` 报错，**大概率是这两处**：

1. **I2C API 版本差异**。`ssd1306.c` 已按 `ESP_IDF_VERSION_MAJOR` 分成两套实现
   （v4.4~v5.x 用 legacy driver，v6.x 用新 `i2c_master`）。
   若你的版本介于两者之间且报 `i2c_master_transmit` 未定义，把文件顶部的
   `>= 6` 改成 `>= 5` 或 `>= 4` 试一下即可。
2. **Kconfig 选项名变化**。`sdkconfig.defaults` 里只放了各版本都存在的通用项，
   如果提示某个 `CONFIG_xxx` 未知，直接删掉那一行。

其余（gpio / esp_timer / esp_random / FreeRTOS）都是十年没变过的稳定 API。

---

## 9. 许可与来源

- ASCII 5x7 点阵来自 [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)（BSD 许可）
- 汉字点阵由 `tools/gen_font.py` 用系统 Noto Sans CJK 现场渲染生成
- 其余代码为本工程实现，可自由使用和修改

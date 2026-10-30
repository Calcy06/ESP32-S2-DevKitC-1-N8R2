# ESP32-S2 有源高电平触发蜂鸣器示例（纯手工配置版）

蜂鸣器每 **3 秒鸣响 1 秒**，循环执行。

**本版本只有 5 个源文件**，不含 `sdkconfig.defaults`，也不含 `Kconfig.projbuild`。
所有配置分成两处，各管各的：

| 配什么 | 在哪配 | 怎么生效 |
|---|---|---|
| 芯片型号、FreeRTOS、Flash、日志等级等**框架级**配置 | 命令行 `set-target` + `idf.py menuconfig` | 写进 `sdkconfig` → 生成 `sdkconfig.h` → 重新 build |
| 蜂鸣器**引脚 / 周期 / 时长**等应用参数 | `main/main.c` 顶部的 5 个 `#define` | 直接改代码 → 重新 build |

```
buzzer/
├── CMakeLists.txt            # 顶层工程入口
├── README.md
└── main/
    ├── CMakeLists.txt        # 组件注册
    ├── buzzer.c / buzzer.h   # 蜂鸣器 HAL：init / set / on / off
    └── main.c                # 参数 #define + app_main + 3s 周期 1s 鸣响
```

---

## 1. 从零开始：一步步跑通

### Step 0 —— 准备 ESP-IDF 环境

每个**新开的终端**都要先导出环境（你用的是 v6.0.2）：

```bash
. $IDF_PATH/export.sh          # Windows 用 $IDF_PATH/export.bat
idf.py --version               # 能打印版本号就说明环境 OK
```

### Step 1 —— 放置工程文件

把整个 `buzzer` 文件夹拷到虚拟机，比如 `~/test/buzzer`。
因为这次删掉了配置文件，**建议把旧的构建产物一起清掉**，避免残留的 `CONFIG_BUZZER_*` 干扰：

```bash
rm -rf ~/test/buzzer/build ~/test/buzzer/sdkconfig
cd ~/test/buzzer
```

> 想连我给的文件都不用、真正从空工程自己搭一遍，看**附录 A**。

### Step 2 —— 自己指定芯片型号（⚠️ 本版本必做）

```bash
idf.py set-target esp32s2
```

**这一步在旧版本里可以靠 `sdkconfig.defaults` 里的 `CONFIG_IDF_TARGET="esp32s2"` 自动完成，
现在文件删了，就必须你手动做。** 如果不做直接 `idf.py build`，IDF 会按默认目标
（通常是 `esp32`）生成 `sdkconfig`，编出来的固件和 S2 不匹配，烧录会失败或跑不起来。

这条命令做的事：创建/重建工程根目录的 `sdkconfig`，并写入
`CONFIG_IDF_TARGET="esp32s2"`。**生成出来的 `sdkconfig` 就是原来 `sdkconfig.defaults` 想给的东西。**

验证：

```bash
grep CONFIG_IDF_TARGET sdkconfig        # 应显示 CONFIG_IDF_TARGET="esp32s2"
```

> 另一种等价写法是 `idf.py -DIDF_TARGET=esp32s2 build`，但 `set-target` 是官方标准流程，
> 它还会顺带清理不匹配的构建缓存，推荐用它。

### Step 3 —— 自己配框架参数（可选，但建议练一遍）

```bash
idf.py menuconfig
```

这一步配的是 **ESP-IDF 框架本身**，不是蜂鸣器引脚（原因见第 5 节）。
先按 `S` 保存、`Q` 退出即可跳过；想练手就照 **5.5 的四个练习**做。

按键速查：`↑↓` 移动 · `Enter` 进入/确认 · `空格` 勾选 · **`/` 搜索配置项** ·
`?` 看帮助 · **`S` 保存** · **`Q` 退出**。

> 菜单里改完**必须重新 build** 才生效 —— 这是编译期配置，不是运行时参数。

### Step 4 —— 自己写代码配蜂鸣器

打开 `main/main.c`，改顶部这 5 行：

```c
#define BUZZER_GPIO_NUM   GPIO_NUM_4   /* ① 控制引脚 */
#define BUZZER_ACTIVE_LVL 1            /* ② 1=高电平触发, 0=低电平触发 */
#define BUZZER_PERIOD_MS  3000         /* ③ 周期 ms */
#define BUZZER_ON_MS      1000         /* ④ 周期内鸣响时长 ms */
#define BUZZER_USE_SW_TIMER 0          /* ⑤ 0=FreeRTOS任务, 1=软件定时器 */
```

| 宏 | 怎么改 | 注意事项 |
|---|---|---|
| ① `BUZZER_GPIO_NUM` | 换成 `GPIO_NUM_5`、`GPIO_NUM_12` 等 | **别用** GPIO26~32 / 0 / 45 / 46 / 19 / 20 / 43 / 44，见第 4 节 |
| ② `BUZZER_ACTIVE_LVL` | 低电平触发模块改成 `0` | HAL 会自动换算输出电平，`buzzer_on()` 语义不变 |
| ③ ④ 周期/时长 | 比如改成 `5000` / `500` | 编译期 `_Static_assert` 会拦住「响的时长 ≥ 周期」 |
| ⑤ 实现方式 | 改成 `1` 体验软件定时器 | 软件定时器方案要求周期和时长都是 **1000 的整数倍**，否则编译报错 |

**为什么现在只能写代码配**：IDF 自带的菜单里没有"蜂鸣器引脚"这一项 —— 蜂鸣器是你自己的外设，
IDF 不认识它。想让它出现在菜单里就得自己写 `Kconfig.projbuild` 声明（见 5.2）。

改完保存，进入下一步。

### Step 5 —— 编译、烧录、监视

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor    # Linux 虚拟机常用 ttyUSB0 / ttyACM0
# 退出 monitor：Ctrl + ]
```

烧录口不确定时用 `ls /dev/ttyUSB* /dev/ttyACM*` 看插拔前后多了哪个设备。

**成功标志**：上电短"嘀"150ms，之后响 1s、停 2s，日志每 3 秒一组：

```
I (491)  buzzer_demo: BEEP ON  | GPIO4 = 1 | 1000 ms
I (1491) buzzer_demo: BEEP OFF | GPIO4 = 0 | 2000 ms
I (3491) buzzer_demo: BEEP ON  | GPIO4 = 1 | 1000 ms
```

时间列差值约 1000 / 2000 ms 即正确。更多验证手段见第 6 节。

---

## 2. 硬件驱动方式

### 2.1 有源 vs 无源蜂鸣器

| | 有源蜂鸣器（本例） | 无源蜂鸣器 |
|---|---|---|
| 内部 | 自带振荡电路 | 只有线圈/压电片 |
| 激励 | 直流电压即可发声 | 必须给 2~5kHz 方波（PWM） |
| 音调 | 固定频率，改不了 | 可调频 |

> 有源蜂鸣器只需 GPIO 输出高低电平，不需要 PWM / LEDC。
> 若手上的是无源蜂鸣器，这段代码只能听到"咔哒"一声，需改用 LEDC 输出方波。

### 2.2 方案 A（推荐）：NPN 三极管 / N-MOS 驱动

有源蜂鸣器一般 5V、工作电流 **20~30mA**，远超单个 GPIO 的舒适区，
且 5V 与 ESP32-S2 的 3.3V IO 不兼容 —— **不能直接把 GPIO 接到 5V 蜂鸣器上**。

```
                    +5V
                     │
                     ├────────────┐
                     │            │
                  ┌──┴──┐      ═══╪═══  0.1µF 去耦（建议）
                  │蜂鸣器│      │  │
                  └──┬──┘      │  │
                     │         │  │
       1N4148 ┌──────┤         │  │    阴极(带环一端)朝 +5V
       续流二极管 ────┘         │  │
                     │ C       │  │
   GPIO4 ──[1kΩ]──B──┤ NPN     │  │   NPN：S8050 / 2N3904 / MMBT3904
                     │ E       │  │
                    GND ───────┴──┴── 必须与 ESP32-S2 共地！
                     ▲
              GPIO ──┴──[10kΩ]──GND   下拉，防上电误鸣
```

* **N-MOS 替代**：2N7002 / AO3400，G 串 100Ω，S→GND，D→蜂鸣器负极，G 对地 10kΩ。
* **基极电阻 1kΩ**：(3.3−0.7)/1k ≈ 2.6mA 基流，β≈100 可驱动 200mA 以上。
* **1N4148 续流二极管**：电磁式蜂鸣器是感性负载，关断尖峰会击穿三极管，
  **必须反并联在蜂鸣器两端（阴极朝 VCC）**。
* **10kΩ 下拉**：上电到 `gpio_config()` 生效之间引脚高阻，基极浮空会**上电误鸣**，
  硬件下拉可消除。`gpio_config` 的内部下拉在输出模式下救不了这个阶段。
* **共地**：5V 电源 GND 必须和 ESP32-S2 的 GND 连一起。

### 2.3 方案 B：GPIO 直驱（仅限 3.3V、≤10mA 小蜂鸣器）

```
GPIO4 ──[100Ω ~ 330Ω 限流]──→ 蜂鸣器 + ──→ 蜂鸣器 − ──→ GND
```

限流电阻**必须串**（驱动能力档位不是限流保护）；>10mA 请用方案 A。

---

## 3. GPIO 配置要点（`gpio_config`）

代码在 `main/buzzer.c` 的 `buzzer_init()`：

```c
gpio_config_t io_conf = {
    .pin_bit_mask   = (1ULL << gpio),        // 用 1ULL，别用 1U（引脚号可到 46）
    .mode           = GPIO_MODE_OUTPUT,      // 推挽输出
    .pull_up_en     = GPIO_PULLUP_DISABLE,
    .pull_down_en   = GPIO_PULLDOWN_DISABLE,
    .intr_type      = GPIO_INTR_DISABLE,
};
gpio_config(&io_conf);
gpio_set_level(gpio, !active_level);          // 先给关闭电平，避免配置瞬间误鸣
gpio_set_drive_capability(gpio, GPIO_DRIVE_CAP_1);
```

| 字段 | 取值 | 原因 |
|---|---|---|
| `pin_bit_mask` | `(1ULL << gpio)` | `1U` 在 gpio ≥ 32 时溢出，S2 最高到 46 |
| `mode` | `GPIO_MODE_OUTPUT` 推挽 | 高电平触发要 IO 主动拉到 3.3V；只有「低电平有效 + 外部上拉 5V」才用开漏 |
| `pull_up_en` | `DISABLE` | 推挽输出下内部上拉无意义，还会并联 45kΩ |
| `pull_down_en` | `DISABLE` | 同上；上电误鸣请靠**硬件**下拉 |
| `intr_type` | `GPIO_INTR_DISABLE` | 纯输出负载，不需要中断 |
| 驱动能力 | `GPIO_DRIVE_CAP_1`（≈10mA） | 只驱动三极管基极时电流极小 |

`buzzer_init()` 还做了 `GPIO_IS_VALID_OUTPUT_GPIO(gpio)` 校验，能挡住 GPIO46（仅输入）这类非法引脚。

---

## 4. 引脚选型（ESP32-S2）

### ❌ 不推荐 / 禁止

| 引脚 | 原因 |
|---|---|
| **GPIO26~32** | 内部 SPI flash / PSRAM，接了直接起不来 |
| **GPIO0** | Strapping（boot 模式），外接强拉会导致无法启动 |
| **GPIO45** | Strapping（VDD_SPI 电压 3.3V/1.8V），误配可能损坏 flash |
| **GPIO46** | Strapping + **仅输入（固定下拉）**，不能做输出 |
| **GPIO39~42** | 默认 JTAG 在线调试 |
| **GPIO43 / GPIO44** | UART0 默认 TX/RX，烧录与日志控制台占用 |
| **GPIO19 / GPIO20** | 原生 USB D−/D+，用 USB 下载/日志时不要动 |
| **GPIO18 / GPIO2** | Saola-1 的板载 RGB LED 在 18；DevKitM-1 的板载 LED 在 2 |

### ✅ 推荐

**GPIO4**（本工程默认）：非 strapping、非 USB、非 flash、非 UART0、非 JTAG。
备选 GPIO5、12~17、21。GPIO11~20 属 ADC2，**Wi-Fi 开启时 ADC2 读数无效**（数字 IO 不受影响）。

---

## 5. 那两个被删掉的文件，分别是什么？

### 5.1 `sdkconfig.defaults` —— "预设的编译期配置"

它只在 **`sdkconfig` 不存在时**被读取，用来给 Kconfig 选项塞默认值。原来里面只有一行：

```
CONFIG_IDF_TARGET="esp32s2"
```

删掉它 = 没有预设。**替代做法就是 Step 2 的 `idf.py set-target esp32s2`**，
效果完全一样，只是从"文件自动"变成"你手动执行一条命令"。

想加回来：在**工程根目录**新建 `sdkconfig.defaults`，一行一项写 `CONFIG_XXX=值`：

```
CONFIG_IDF_TARGET="esp32s2"
CONFIG_FREERTOS_HZ=1000
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
```

> 注意：已有 `sdkconfig` 时这个文件**不会覆盖**它，一切以 menuconfig 里改的为准。
> 想让 defaults 生效，先 `rm sdkconfig`（或直接 `idf.py fullclean`）。

### 5.2 `Kconfig.projbuild` —— "自定义菜单的声明"

IDF 自带几千个 Kconfig 选项，但**没有"蜂鸣器引脚"** —— 它不认识你的外设。
这份文件的作用就是告诉 IDF："我有这么个配置项、默认值多少、取值范围多少"，
然后它才会出现在 menuconfig 顶层。

删掉它 = 你的应用参数只能写在代码里（本版本的做法）。
想加回来：在 `main/` 下新建 `Kconfig.projbuild`：

```
menu "Buzzer Example Configuration"

    config BUZZER_GPIO
        int "Buzzer control GPIO number"
        range 0 21
        default 4
        help
            驱动蜂鸣器的 GPIO。推荐 GPIO4，避开 GPIO26~32 / 0 / 45 / 46 / 19 / 20 / 43 / 44。

    config BUZZER_PERIOD_MS
        int "Beep cycle period (ms)"
        default 3000

    config BUZZER_ON_MS
        int "Beep duration (ms)"
        default 1000

    choice BUZZER_IMPL
        prompt "Buzzer control implementation"
        default BUZZER_USE_TASK
        config BUZZER_USE_TASK
            bool "FreeRTOS task (recommended)"
        config BUZZER_USE_SW_TIMER
            bool "FreeRTOS software timer callback"
    endchoice

endmenu
```

代码里改成读配置（需 `#include "sdkconfig.h"`）：

```c
#define BUZZER_GPIO_NUM  ((gpio_num_t)CONFIG_BUZZER_GPIO)
#define BUZZER_PERIOD_MS (CONFIG_BUZZER_PERIOD_MS)
#if defined(CONFIG_BUZZER_USE_SW_TIMER) && CONFIG_BUZZER_USE_SW_TIMER
#define BUZZER_USE_SW_TIMER 1
#else
#define BUZZER_USE_SW_TIMER 0
#endif
```

**三条铁律**：

1. `main/CMakeLists.txt` **保持原样**，不要加 `KCONFIG_PROJBUILD` 参数，IDF 会自动扫描组件目录。
2. ⚠️ 曾踩过的坑：写 `KCONFIG_PROJBUILD "Kconfig.projbuild"`（相对路径），
   v6.0.2 会按**工程根目录**解析，而文件在 `main/` 下 → 报 `'Kconfig.projbuild' not found`。
   真要显式声明就用绝对路径：`KCONFIG_PROJBUILD "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig.projbuild"`。
3. 增删 `Kconfig.projbuild` 或改了 Kconfig 结构后，**必须 `idf.py fullclean`** 再配置，
   否则旧的 `kconfig_projbuild.in` 缓存会残留。

Kconfig 语法速查：`config X` 定义项 / `int` `bool` `string` 类型 / `default` 默认值 /
`range` 数值范围 / `help` 帮助文本 / `menu...endmenu` 分组 /
`choice...endchoice` 单选组 / `depends on` 依赖。

### 5.3 `menuconfig` 到底有什么用

**一句话**：它配的是**整个 ESP-IDF 框架**，不是你那几个 GPIO。
IDF 自带几千个 Kconfig 项，不用你写任何 Kconfig 就能用。原理是：

```
各组件自带的 Kconfig 文件（定义菜单项和默认值）
   ↓  idf.py menuconfig —— 你在界面里选择
sdkconfig                       ← 工程根目录的纯文本，保存你的选择
   ↓  idf.py build 时由 kconfgen 生成
build/config/sdkconfig.h        ← 每个选项变成 #define CONFIG_XXX
   ↓  代码 #include "sdkconfig.h"
决定哪些代码被编译、参数取多少
```

### 5.4 和 STM32 CubeMX 的区别（两种范式）

| | STM32 CubeMX | ESP-IDF menuconfig |
|---|---|---|
| 本质 | **代码生成器** | **编译期配置器** |
| 输出 | 生成 C 初始化代码：`MX_GPIO_Init()`、时钟树、HAL 配置 | 只生成 `sdkconfig.h` 里的 `#define CONFIG_XXX` |
| 配置引脚 | 图形化点选引脚、自动分配复用、生成 GPIO 初始化代码 | **没有对应机制**，`gpio_config()` 要自己写 |
| 配内核/中间件 | 图形化配 FreeRTOS、FATFS、USB 等 | menuconfig 配 FreeRTOS、LwIP、Wi-Fi、分区表等 |
| 改完配置后 | 重新生成代码 | 重新 `idf.py build` |
| 自定义应用参数 | `.ioc` 里加自定义项很有限 | 写 `Kconfig.projbuild` 声明 |

**ESP-IDF 没有 CubeMX 那种「点几下引脚就自动出初始化代码」的工具。**
它把事情拆成两半：**框架级配置**交给 menuconfig，**外设初始化代码**由你自己写。
VS Code 的 ESP-IDF 扩展里那个 *SDK Configuration Editor* 只是 menuconfig 的 GUI 外壳，不生成 C 代码。

### 5.5 menuconfig 能配什么 + 四个练习

| 类别 | 例子 |
|---|---|
| 芯片与存储 | 目标芯片、CPU 频率（80/160/240MHz）、Flash 大小/模式/速度、分区表、PSRAM |
| FreeRTOS | Tick rate (Hz)、任务栈大小、内核行为、钩子函数 |
| 日志 | 默认等级、格式、颜色、**输出到 UART0 还是 USB Serial-JTAG** |
| 网络 | Wi-Fi 参数、LwIP 缓冲区、mDNS、DHCP、HTTP 服务器 |
| 电源 | 电源管理、DFS、Light-sleep/Deep-sleep 唤醒源、看门狗 |
| 安全 | Secure Boot、Flash 加密 |
| 组件裁剪 | 关掉用不到的组件，显著减小固件体积 |
| 烧录 | 串口、波特率、烧录前行为、压缩上传 |

在 Step 3 的菜单里依次练这四项，**都不需要写任何 Kconfig**：

**练习 1 —— FreeRTOS 节拍**：按 `/` 搜 `FREERTOS_HZ`，通常在
`Component config → FreeRTOS → Kernel → Tick rate (Hz)`，把 `100` 改成 `1000`。

> 说明：IDF 默认 100Hz（10ms/tick），**不改也能正常运行**，本工程 1 秒延时在 100Hz 下
> 恰好是整数个 tick、误差最多约 10ms。改成 1000Hz 只是让节拍粒度和日志时间戳更细。

**练习 2 —— 日志等级**：`Component config → Log output → Default log verbosity`
设成 `Warning`，所有 `ESP_LOGI` 都不输出，效果很直观。

**练习 3 —— Flash 大小**：`Serial flasher config → Flash size`（按板子选 4MB）。

**练习 4 —— CPU 频率**：`Component config → ESP System Settings → CPU frequency`（80/160/240 MHz）。

`S` 保存 → `Q` 退出 → 验证：

```bash
grep CONFIG_FREERTOS_HZ sdkconfig
idf.py build
grep CONFIG_FREERTOS_HZ build/config/sdkconfig.h    # 应看到 #define CONFIG_FREERTOS_HZ 1000
```

`sdkconfig`（你选的）和 `build/config/sdkconfig.h`（给 C 代码用的）两边都是 1000 就通了。

---

## 6. 验证运行结果

**① 听**：上电自检短鸣 150ms，之后每 3 秒响 1 秒（响 1s → 停 2s）。

**② 看日志**（任务方式）：

```
I (321)  buzzer_demo: ESP32-S2 active buzzer demo (period 3000 ms, on 1000 ms)
I (331)  buzzer: buzzer init: GPIO4, active_level=1 (高电平触发)
I (491)  buzzer_demo: startup self-test beep done
I (491)  buzzer_demo: BEEP ON  | GPIO4 = 1 | 1000 ms
I (1491) buzzer_demo: BEEP OFF | GPIO4 = 0 | 2000 ms
I (3491) buzzer_demo: BEEP ON  | GPIO4 = 1 | 1000 ms
```

时间列差值应约等于 1000 / 2000 ms。把 `BUZZER_USE_SW_TIMER` 改成 `1` 后，
日志格式变为 `timer tick N (slot M) -> BEEP ON/OFF`。

> 若日志里看不到 `ESP_LOGI` 行，回想练习 2：日志等级被设成 `Warning` 了。

**③ 示波器/万用表**：GPIO4 对 GND 应为周期 3s、占空比 1/3 的方波，
高电平 ≈ 3.3V，低电平 ≈ 0V。

**④ 隔离法**：把 `BUZZER_GPIO_NUM` 改成一个没接东西的引脚，重新 build，
日志照常但无声 → 说明软件 OK，问题在硬件/接线。

---

## 7. 常见接线错误与注意事项

| 现象 | 原因 | 解决 |
|---|---|---|
| 编译/烧录失败，报目标芯片不匹配 | 没执行 `idf.py set-target esp32s2` | 本版本无 `sdkconfig.defaults`，必须手动指定 |
| 完全不响 | 蜂鸣器正负极接反 / 负极没接三极管 C | 有源蜂鸣器分正负极，长脚一般为 + |
| 完全不响 | 5V 电源 GND 没和 ESP32-S2 共地 | 必须共地 |
| 完全不响 | 用的是**无源**蜂鸣器 | 换有源，或改 LEDC 输出 2~5kHz PWM |
| 声音很小 | 三极管没饱和 / BCE 接错 | 1kΩ 基极电阻；S8050 与 2N3904 的 BCE 顺序相反 |
| 上电"嘀"一声 | 高阻期基极浮空 | GPIO 到 GND 加 10kΩ 下拉 |
| 芯片反复重启 | 蜂鸣器电流拖垮电源 | 独立供电 + 共地；VCC 加 100µF + 0.1µF |
| 烧录失败 / 起不来 | 用了 GPIO0/45/46 或 26~32 | 换 GPIO4 |
| 串口无日志 | 占了 GPIO43/44，或日志等级设成 Warning | 换引脚 / 调回 Info |
| 用 GPIO19/20 后 USB 失效 | 这两个脚是原生 USB D−/D+ | 换引脚 |
| 周期略有偏差 | FreeRTOS 节拍 100Hz | 按练习 1 把 `Tick rate (Hz)` 设为 1000 |

其他：

* **任何时候都不要**让 GPIO 输出超过 3.3V 或吸入超额电流。
* 蜂鸣器与 Wi-Fi 天线留点距离，强电流回路会干扰 RF。
* 若进 Deep-sleep，用 `gpio_hold_en()` 保持蜂鸣器引脚电平，避免睡眠期间误鸣。
* 换成继电器（感性更强）时务必保留续流二极管并加大余量。

---

## 附录 A：完全不用现成文件，从空工程自己搭一遍

如果想真正理解每个文件的作用，可以不用这个文件夹，从头建：

```bash
. $IDF_PATH/export.sh
idf.py create-project buzzer_manual
cd buzzer_manual
idf.py set-target esp32s2
```

`create-project` 会生成：

```
buzzer_manual/
├── CMakeLists.txt              # 已写好 project(buzzer_manual)，不用改
├── sdkconfig                   # set-target 生成（不是 defaults 文件）
└── main/
    ├── CMakeLists.txt
    └── buzzer_manual.c
```

接下来自己动手：

1. **改名**：`mv main/buzzer_manual.c main/main.c`
2. **写 `main/CMakeLists.txt`**（`create-project` 生成的只列了旧文件名，必须同步改）：

   ```cmake
   idf_component_register(
       SRCS "main.c" "buzzer.c"
       INCLUDE_DIRS "."
   )
   ```

3. **写 `main/buzzer.h`**：声明 `buzzer_init(gpio_num_t gpio, int active_level)`、
   `buzzer_set(bool on)`、`buzzer_on()`、`buzzer_off()`。
4. **写 `main/buzzer.c`**：核心就是第 3 节那段 `gpio_config_t` + `gpio_set_level()`。
5. **写 `main/main.c`**：`app_main()` 里先 `buzzer_init()`，再用
   `xTaskCreate()` 建任务，任务里 `buzzer_on()` →
   `vTaskDelayUntil(&last, pdMS_TO_TICKS(1000))` → `buzzer_off()` →
   `vTaskDelayUntil(&last, pdMS_TO_TICKS(2000))`，循环。
6. **编译烧录**：`idf.py build` → `idf.py -p /dev/ttyUSB0 flash monitor`

对照本文件夹的 5 个文件，可以逐行验证自己写得对不对。
`create-project` 生成的工程默认也没有 `sdkconfig.defaults` 和 `Kconfig.projbuild`，
所以本版本就是 IDF 官方模板的原生形态。

### 常用辅助命令

```bash
idf.py fullclean                 # 彻底清理（改芯片型号、Kconfig 结构时必须）
idf.py reconfigure               # 只重跑配置，不清编译产物
idf.py -p /dev/ttyUSB0 monitor   # 只监视不烧录
idf.py -p /dev/ttyUSB0 erase-flash
idf.py size                      # 查看固件体积
```

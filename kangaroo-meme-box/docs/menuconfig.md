# `idf.py menuconfig` 一步步配置指南（ESP32-S2-DevKitC-1）

> 适用于本工程 `kangaroo-meme-box`，ESP-IDF v5.x，Linux 虚拟机环境。
> 全程只需要键盘，终端窗口请拉到 **至少 100×30**（太小会报 `Your display is too small`）。

---

## TL;DR：按默认接线，你只需要动 1 处

如果你的接线和 README 一致（OLED SDA→GPIO8、SCL→GPIO9、蜂鸣器→GPIO10、按键用板载 BOOT），
那么**打开 menuconfig 后只需要改一个地方**：把控制台通道从 `USB Serial/JTAG` 切成 `UART0`。

剩下所有项（引脚、地址、帧率、Flash 大小、CPU 频率……）**保持默认，一个都别动**。

最短路径（在 menuconfig 界面里按顺序按）：

```
/               → 输入 ESP_CONSOLE_UART  → 回车
<数字键>         → 跳到匹配的那一项
Space           → 选中（显示变成 (*) UART0）
Q               → 选 < Yes > 回车
```

然后 `idf.py build`。手把手版见下面第 3~7 节。

---

## 第 0 步：先搞懂它在干什么

`menuconfig` 改的是工程根目录下的 **`sdkconfig`** 文件，本质是一堆 `CONFIG_XXX=y` 的宏。
代码里 `board_config.h` 用 `#ifndef CONFIG_xxx` 读的就是它。

本工程已经自带了一份 `sdkconfig.defaults`，**上面 90% 的项已经帮你设好了**。
所以你真正需要手动改的只有两类：

| 类别 | 内容 | 是否必须改 |
|------|------|-----------|
| A. 本工程菜单 | 引脚、I2C 地址、帧率 | 一般不用改，接线不同才改 |
| B. ESP32-S2 控制台通道 | 用哪个 USB 口看日志 | **虚拟机用户基本必改** |

---

## 第 1 步：生成 sdkconfig

```bash
cd ~/kangaroo-meme-box
. $IDF_PATH/export.sh          # 没配过环境变量就先跑这句
idf.py set-target esp32s2
```

这一步会读 `sdkconfig.defaults` 生成 `sdkconfig`，并创建 `build/`。
**必须先做**，否则直接开 menuconfig 会按 esp32（默认 target）生成一套错的配置。

看到 `Build files have been written to: .../build` 就对了。

---

## 第 2 步：打开配置界面

```bash
idf.py menuconfig
```

### 键盘操作速查

| 按键 | 作用 |
|------|------|
| `↑` `↓` | 上下移动 |
| `Enter` | 进入子菜单 / 进入当前项的输入框 |
| `Space` | 勾选 / 取消 `[*]` 开关 |
| `Y` / `N` | 同上（部分终端用这个） |
| `?` | 查看当前项的帮助（help 文本） |
| `/` | **搜索**（最有用，见下） |
| `ESC` × 2 | 退回上一级 / 退出 |
| `S` | 保存（保存到 `sdkconfig`） |
| `Q` | 退出（会问你要不要保存） |

### 搜索怎么用

按 `/` → 输入 `OLED_SDA` → 回车，会列出匹配项并显示跳转编号（如 `(1)`），按数字键 `1` 直接跳过去。
**找不到本工程菜单时就用这招**，比一层层翻快得多。

---

## 第 3 步：配置本工程菜单「袋鼠整活盒」

按 `/` 搜 `OLED_SDA` 跳进去，或者直接翻到：

```
袋鼠整活盒 (Kangaroo Meme Box)
```

进去后一共 6 项，逐项说明：

### 3.1 `OLED SDA 引脚 (I2C)` —— 默认 8

| 你的接法 | 填什么 |
|----------|--------|
| SDA 接 GPIO8（默认接线） | **8**（不用动） |
| 你自己换了别的脚 | 填实际 GPIO 号 |

⚠️ **绝对不要用 GPIO26~32**（板载 8MB Flash + 2MB PSRAM 占用）、**GPIO45/46**（strapping）、**GPIO19/20**（这是 USB 口，动了日志就没了）。

### 3.2 `OLED SCL 引脚 (I2C)` —— 默认 9

同上，接 GPIO9 就保持 9。

### 3.3 `OLED I2C 地址 (7位)` —— 默认 `0x3C`

**这是唯一需要你看一眼硬件的一项。**

把 OLED 模块翻过来，背面通常有一个标着 `0x78` / `0x7A` 的贴片电阻焊盘：

```
        ┌─────────┐
        │  0x78   ●──────   ← 电阻焊在这边 → 地址 0x3C
        │         │
        │  0x7A   ●──────   ← 焊在这边   → 地址 0x3D
        └─────────┘
```

大多数 0.96 寸 / 1.3 寸模块出厂是 **0x3C**，保持默认即可。
**改错了的表现**：日志刷 `init cmd failed` 或 `first flush failed`，屏幕全黑。

> 不确定就两个都试一遍，30 秒的事。

### 3.4 `蜂鸣器控制引脚` —— 默认 10

高电平触发的有源蜂鸣器。接 GPIO10 就保持 10。
如果用三极管驱动，这里填的是 **接三极管基极电阻的那个 GPIO**，不是蜂鸣器引脚。

### 3.5 `触发按键引脚` —— 默认 0

**0 = 板载 BOOT 键**，什么都不用接，保持默认。
- 短按（<1.2s）→ 换下一句台词
- 长按（≥1.2s）→ 静音 / 取消静音

想外接按键才需要改。⚠️ 外接时别选 GPIO45/46（strapping），改错会导致芯片起不来。

### 3.6 `刷新间隔 (ms)` —— 默认 40

40ms ≈ 25fps。想更流畅可以改 33（30fps），但 I2C 400kHz 传一屏要 ~23ms，再快就掉帧了。

### 3.7 `待机自动播放间隔 (秒)` —— 默认 10

待机时每 10 秒自动来一句。填 **0** 就只靠按键触发（想安静点选这个）。

---

## 第 4 步（关键）：配置 ESP32-S2 的控制台通道

**这一步是 ESP32-S2 + 虚拟机最容易翻车的地方，务必看。**

ESP32-S2-DevKitC-1 板上有 **两个 USB 口**，功能完全不同：

```
┌─────────────────────────────────┐
│  ┌────┐              ┌────┐     │
│  │USB │              │UART│     │   ← 看丝印
│  └────┘              └────┘     │
│  GPIO19/20           经 CP2102N │
│  USB-Serial-JTAG     转 U0TXD/RXD│
└─────────────────────────────────┘
```

进入菜单路径：

```
Component config  →  ESP System Settings  →  Channel for console output
```

（按 `/` 搜 `CONSOLE` 更快）

三个选项怎么选：

| 选项 | 插哪个口 | 设备节点 | 虚拟机注意事项 |
|------|----------|----------|----------------|
| `USB Serial/JTAG`（**IDF 默认**） | 标 **USB** 的口 | `/dev/ttyACM0` | ⚠️ 需要在虚拟机里做 **USB 设备直通**，且插拔后节点号可能变 |
| `UART0`（**推荐虚拟机用**） | 标 **UART** 的口 | `/dev/ttyUSB0` | ✅ 普通 USB 串口，透传稳定，进下载模式也自动 |
| `USB Serial/JTAG (secondary)` | — | — | 本项目用不到 |

### 我的建议：虚拟机用户选 UART0

改法：

```
Component config
  └─ ESP System Settings
       └─ Channel for console output
            ( ) USB Serial/JTAG          ← 取消
            (*) UART0                    ← 选这个
```

为什么：S2 的 USB-Serial/JTAG 是芯片内部的 USB 外设，插上后出现的是 **USB 设备**（不是串口设备），虚拟机必须做 USB 直通，而且它在烧录模式和应用模式间会**重新枚举**（VID/PID 变化），经常被宿主机抢走。走 UART 口就没这些破事，esptool 通过 DTR/RTS 自动拉 GPIO0 进下载模式，一条命令搞定。

选完 UART0 后顺手确认一下波特率：

```
Component config  →  ESP System Settings  →  UART console baud rate
```
保持 **115200** 即可。

---

## 第 5 步：核对 sdkconfig.defaults 已设好的项

下面这些 `sdkconfig.defaults` 里已经写了，理论上不用动。但**如果你的 IDF 版本较老（v4.4）某些符号名不同，它们会静默失效**，所以扫一眼确认：

| 菜单路径 | 值 | 说明 |
|----------|-----|------|
| `Serial flasher config → Flash size` | **8MB** | N8R2 = 8MB Flash。设小了能跑但浪费；设大了烧录报 `Detected size larger` |
| `Component config → ESP System Settings → CPU frequency` | **240 MHz** | 单核 S2，刷屏吃 CPU，拉满 |
| `Component config → FreeRTOS → Tick rate (Hz)` | **1000** | 决定 `vTaskDelay(40)` 是否真的是 40ms |
| `Component config → Log output → Default log verbosity` | **Info** | 想看更详细的调试信息可改 Debug |
| `Component config → ESP System Settings → Main task stack size` | **8192** | 帧缓冲是静态的，8K 够 |

核对方法：退出 menuconfig 后

```bash
grep -E "FLASHSIZE|CPU_FREQ|FREERTOS_HZ" sdkconfig
```

---

## 第 6 步：不用动的部分

这些保持默认，动了反而出问题：

- **Partition Table** —— 默认 `Single factory app, no OTA`，本项目一个 app 就够
- **Wi-Fi / Bluetooth** —— 本项目不联网，S2 也**没有蓝牙**。关掉能省 Flash，但默认不动也能编译通过
- **PSRAM** —— N8R2 带 2MB PSRAM，本工程帧缓冲在内部 RAM（1KB），**不需要开 PSRAM**。开了反而占 GPIO26~32
- **Compiler options → Optimization Level** —— 默认 Debug (`-Og`)。想让动画更顺可以改成 `Release (-Os)` / `(-O2)`

---

## 第 7 步：保存退出

1. 按 **`Q`**
2. 弹窗问 `Save configuration?` → 选 **`Yes`**（← → 移动，Enter 确认）
3. 界面退出，终端显示 `Running cmake...` 自动重新配置

> 想放弃改动：按 `Q` 选 `No`，或者直接删掉 `sdkconfig` 重新 `idf.py set-target esp32s2` 就会从 defaults 重生。

---

## 第 8 步：编译烧录

```bash
idf.py build
```

看到 `Project build complete.` 就成了。然后：

```bash
# 用 UART 口（第 4 步选了 UART0 的情况）
idf.py -p /dev/ttyUSB0 -b 460800 flash monitor

# 用 USB 口（选了 USB Serial/JTAG 的情况）
idf.py -p /dev/ttyACM0 flash monitor
```

`monitor` 退出按 **`Ctrl+]`**。

---

## 第 9 步：验证配置真的生效了

烧完看到日志：

```
I (xxx) kangaroo: OLED init ok (SDA=8 SCL=9 addr=0x3C)
I (xxx) kangaroo: buzzer on GPIO10, button on GPIO0
```

如果屏幕不亮，按这个顺序排查：

| 现象 | 原因 | 处理 |
|------|------|------|
| 日志正常但屏全黑 | I2C 地址错 | 改 `0x3C` ↔ `0x3D` 重试 |
| `init cmd failed` | SDA/SCL 反了 或 没共地 | 检查接线 |
| `i2c timeout` | 用了 GPIO26~32 | 改回 GPIO8/9 |
| 屏幕亮但整体左移 2px | 买到的是 SH1106 | 见 `main/ssd1306.c` 里 SH1106 那段注释 |
| 烧录报 `Permission denied` | 串口没权限 | `sudo usermod -aG dialout $USER` 后**注销重登** |
| 烧录报 `Failed to connect` | 没进下载模式 | 按住 BOOT → 点一下 EN → 松开 BOOT，再跑 flash |

---

## 附：menuconfig 打不开 / 报错怎么办

| 报错 | 解决 |
|------|------|
| `Your display is too small to run Menuconfig` | 终端窗口拉大，或 `ssh` 时把本地窗口放大后重连 |
| `kconfiglib` 相关 Python 报错 | `. $IDF_PATH/export.sh` 重跑；或 `rm -rf build sdkconfig` 重建 |
| 找不到「袋鼠整活盒」菜单 | 确认 `main/Kconfig.projbuild` 存在；必须先跑过 `set-target` |
| 菜单是英文方框乱码 | 终端编码设成 UTF-8：`export LANG=C.UTF-8` |

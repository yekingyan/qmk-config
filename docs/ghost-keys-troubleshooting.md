# Ferris Sweep + RP2040 Pro Micro 兼容主控：Ghost Keys 问题求助

## 一句话描述

Ferris Sweep (34键 direct pin 分体键盘) 使用 RP2040 Pro Micro 兼容主控，刷入 QMK (vial-qmk) 固件后，GP26-29 (Pro Micro 别名 F4-F7) 对应的 4 个按键持续自动触发，输出 `werttttt`。不接 PCB 纯主控裸板也复现。已尝试多种软件修复方案均无效。

---

## 项目背景

### 键盘

- **型号**：Ferris Sweep (又名 Cradio)，34 键分体键盘
- **矩阵方式**：**DIRECT_PINS**（无二极管，每个按键直连一个 GPIO，不是传统二极管矩阵）
- **原版设计 MCU**：ATmega32U4 (Pro Micro)
- **当前使用 MCU**：RP2040 Pro Micro 兼容主控，通过 QMK 的 `CONVERT_TO = rp2040_ce` 做引脚映射

### 主控

- **商品标题**：`树莓派迷你开发板ProMicro RP2040兼容Helios OxB2 MicroPython`
- **MCU**：RP2040 (双核 ARM Cortex-M0+ @ 133MHz, 264kB SRAM)
- **闪存**：4MB
- **形态**：Pro Micro 双排针兼容
- **无原理图**：这是淘宝购买的国产兼容板，没有公开原理图

### 固件

- **框架**：vial-qmk (基于 QMK，`vial-kb/vial-qmk` 仓库 `vial` 分支)
- **编译方式**：GitHub Actions 云编译
- **编译命令**：`make ferris/sweep:yekingyan`
- **converter**：`CONVERT_TO = rp2040_ce`（rules.mk 中指定）

### 仓库

- GitHub: `yekingyan/qmk-config`
- keymap 路径: `keyboards/ferris/sweep/keymaps/yekingyan/`

---

## 问题描述

### 现象

1. 刷入固件后，插上 USB，**等几秒**后开始自动输出 `werttttt`
2. `t` 会持续不断输出，不会停止
3. **不接 PCB，纯主控裸板插 USB 也完全一样**
4. W/E/R/T 对应 Sweep 左手第一行第 2-5 列

### 为什么不是传统 ghosting

该设计不是二极管矩阵，不存在传统意义上多键串扰导致的 phantom key。当前异常更像是单脚被持续判低（按下）。

---

## 引脚映射分析（关键）

### 原理图覆盖的 18 个键（半边）

从 Sweep 原理图提取的 SWITCH 到 Pro Micro 引脚映射，再经过 `rp2040_ce` 转换：

```
SWITCH1  → d10 → GP10    SWITCH10 → d1  → GP1
SWITCH2  → d16 → GP16    SWITCH11 → d2  → GP2
SWITCH3  → d14 → GP14    SWITCH12 → d3  → GP3
SWITCH4  → d15 → GP15    SWITCH13 → d4  → GP4
SWITCH5  → d21 → GP21    SWITCH14 → d5  → GP5
SWITCH6  → d18 → GP18    SWITCH15 → d6  → GP6
SWITCH7  → d19 → GP19    SWITCH16 → d7  → GP7
SWITCH8  → d20 → GP20    SWITCH17 → d8  → GP8
SWITCH9  → d9  → GP9     SWITCH18 → d0  → GP0
```

**这 18 个键全部在 GP0-GP21 范围内，没有一个用到 GP26-29。这些键工作正常。**

### 出问题的键（不在原理图覆盖范围内）

Sweep 的 DIRECT_PINS 定义中，第 0 行：

```c
{ E6, F7, F6, F5, F4 }
```

通过 `rp2040_ce` converter 映射：

| 矩阵位置 | 按键 | Pro Micro 别名 | RP2040 GPIO | 特殊性 |
|-----------|------|----------------|-------------|--------|
| (0,0) | Q | E6 | GP7 | 普通 GPIO，**工作正常** |
| (0,1) | W | F7 | GP26 | **ADC0，异常** |
| (0,2) | E | F6 | GP27 | **ADC1，异常** |
| (0,3) | R | F5 | GP28 | **ADC2，异常** |
| (0,4) | T | F4 | GP29 | **ADC3，异常** |

### 关键结论

> **Ferris Sweep 是 34 键，原理图只覆盖了 18 键（半边）。出问题的 W/E/R/T 恰好在原理图缺失的部分，映射到 GP26-29。**

这意味着：
- 同一固件、同一扫描逻辑下，GP0-GP21 的引脚全部正常
- **仅 GP26-29 这 4 个 ADC 复用引脚异常**
- 几乎排除了 QMK bug、DIRECT_PINS 逻辑问题、全局初始化问题

---

## RP2040 GP26-29 的硬件特殊性

根据 RP2040 datasheet：

1. **硬件复位后**，GP26-29 的 `PADS_BANK0` 寄存器默认值为 `0x1F`
   - 其中 **IE (Input Enable, bit 6) = 0**，即输入禁用
   - 其他 GPIO0-GP25) 的默认值为 `0x56`，IE = 1
2. 这是因为 GP26-29 设计为 ADC 输入，硬件默认关闭数字输入以减少噪声
3. 要将 GP26-29 用作普通数字 GPIO，需要：
   - `FUNCSEL = 5` (SIO/GPIO 模式)
   - `IE = 1` (启用输入)
   - `PUE = 1` (启用上拉，对于 direct pin 键盘)

---

## 已排除项

- **PCB 走线短路**：不接 PCB 纯主控裸板也复现
- **单纯按键抖动**：没有按键，裸板就触发
- **传统矩阵 ghosting**：这是 direct pin 设计，不是二极管矩阵
- **QMK direct pin 扫描逻辑问题**：同一逻辑下 GP0-GP21 全部正常
- **全局初始化问题**：只有 GP26-29 异常
- **固件未编译进去**：`wait_ms()` 延迟可观测，代码确实在执行
- **仅靠 QMK 用户层初始化即可修复**：已尝试 board_init / keyboard_post_init_user / matrix_scan_user 多个 hook，均无效

---

## 已尝试的方案（全部无效）

### 方案 1：mcuconf.h 禁用 ADC设

```c
#include_next <mcuconf.h>
#undef RP_ADC_USE_ADC1
#define RP_ADC_USE_ADC1 FALSE
```

目的：阻止 ChibiOS ADC 驱动在 HAL 初始化阶段接管 GP26-29。结果：❌

### 方案 2：halconf.h 关闭 ADC 驱动编译

```c
#include_next <halconf.h>
#undef HAL_USE_ADC
#define HAL_USE_ADC FALSE
```

目的：从编译层面完全移除 ADC 驱动代码。结果：❌

### 方案 3：board_init() 寄存器级修复

```c
#define PADS_BANK0_GPIO(n) (*(volatile uint32_t *)(PADS_BANK0_BASE + 0x04 + (n) * 4))
#define IO_BANK0_GPIO_CTRL(n) (*(volatile uint32_t *)(IO_BANK0_BASE + 0x04 + (n) * 8))

void board_init(void) {
    for (uint8_t pin = 26; pin <= 29; pin++) {
        IO_BANK0_GPIO_CTRL(pin) = 5;  // FUNCSEL = SIO
        PADS_BANK0_GPIO(pin) = (1 << 6) | (1 << 3) | (1 << 1);  // IE + PUE + SCHMITT
    }
}
```

目的：在 ChibiOS `halInit()` 内部直接写 RP2040 寄存器。结果：❌

### 方案 4：keyboard_post_init_user() 二次修复

在 QMK 初始化链的最后一步再次写寄存器。结果：❌

### 方案 5：三层全部叠加

mcuconf.h + halconf.h + board_init() + keyboard_post_init_user() 全部保留。结果：❌

### 诊断尝试

| 诊断方法 | 结果 |
|----------|------|
| `keyboard_post_init_user` 中 `wait_ms(3000)` | ✅ 延迟可观测，代码确实在执行 |
| `keyboard_post_init_user` 中 `send_string("GPIOFIX")` | ❌ 未观测到（USB HID 在此阶段未就绪） |
| `matrix_scan_user` 首次执行时 `send_string("DIAG ...")` | ❌ 被 ghost keys 输出淹没 |
| 寄存器值读取 + 输出 | ❌ 同上，被淹没，无法确认寄存器写入是否真的生效 |

---

## 全链路源码审计发现

### ChibiOS 层

- `RP_PICO_RP2040/board.c`：`__early_init()` 和 `boardInit()` 均为空函数
- `__pal_lld_init()`：只做 `unreset IO_BANK0` 和 `unreset PADS_BANK0`，不碰任何引脚的 PADS 寄存器
- `palSetLineMode(pin, PAL_MODE_INPUT_PULLUP)` 的实现会写 PADS 寄存器（含 IE=1）和 IO_BANK0 CTRL（FUNCSEL=SIO）

### QMK 层

- `matrix_init_pins()`（DIRECT_PINS 模式）对每个引脚调用 `gpio_set_pin_input_high(pin)` → `palSetLineMode(pin, PAL_MODE_INPUT_PULLUP)`
- 理论上 `matrix_init_pins()` 应该能正确配置 GP26-29

---

## 当前代码文件

### rules.mk

```makefile
VIA_ENABLE = yes
VIAL_ENABLE = yes
COMBO_ENABLE = yes
CAPS_WORD_ENABLE = yes
MOUSEKEY_ENABLE = yes
EXTRAKEY_ENABLE = yes
LTO_ENABLE = yes
CONVERT_TO = rp2040_ce
```

### mcuconf.h

```c
#pragma once
#include_next <mcuconf.h>
#undef RP_ADC_USE_ADC1
#define RP_ADC_USE_ADC1 FALSE
```

### halconf.h

```c
#pragma once
#include_next <halconf.h>
#undef HAL_USE_ADC
#define HAL_USE_ADC FALSE
```

### keymap.c 中的修复代码

```c
#define PADS_BANK0_GPIO(n) (*(volatile uint32_t *)(PADS_BANK0_BASE + 0x04 + (n) * 4))
#define IO_BANK0_GPIO_CTRL(n) (*(volatile uint32_t *)(IO_BANK0_BASE + 0x04 + (n) * 8))

void board_init(void) {
    for (uint8_t pin = 26; pin <= 29; pin++) {
        IO_BANK0_GPIO_CTRL(pin) = 5;
        PADS_BANK0_GPIO(pin) = (1 << 6) | (1 << 3) | (1 << 1);
    }
}
```

---

## 编译流程

GitHub Actions 云编译：

1. Checkout 用户仓库 `yekingyan/qmk-config`
2. Checkout `vial-kb/vial-qmk` (ref: `vial`, with submodules)
3. 将用户 keymap 文件复制到 `vial-qmkerris/sweep/keymaps/yekingyan/`
4. `make ferris/sweep:yekingyan`
5. 产出 `.uf2` 文件

刷机方式：双击 reset 进入 UF2 bootloader，拖入 `.uf2` 文件。

---

## 提交历史（按时间倒序）

```
8cfcd4c docs: 记录 ghost keys 验证历程与待排查方向
df271df diag: 诊断移到 matrix_scan_user 首次执行，USB 此时必定 ready
4077d32 diag: USB 枚举等待延长到 3 秒
0ef1566 diag: 先输出寄存器诊断再等10秒
281092e diag: Base 层 W/E/R/T 临时改为 1/2/3/4，验证 keymap 是否被编译进固件
64580c0 diag: 读取 GP26-29 寄存器修复前后实际值
e223220 diag: board_init + keyboard_post_init_user 双保险修复
a662878 fix: 移除 PADS_BANK0_BASE/IO_BANK0_BASE 重复定义
0ba9c0a fix: board_init 强制启用 GP26-29 IE bit + FUNCSEL=SIO
e2b3244 fix: 通过 mcuconf.h 禁用 ADC 外设
0f1db19 fix: 直接写 RP2040 寄存器强制关闭 ADC 功能
b03af1c diag: 启动后自动输出所有 GPIO 状态
3470589 fix: RP2040 ADC 引脚 GP26-29 强制切 GPIO 模式+上拉
```

---

## 当前最接近事实的结论

这块主控虽采用 Pro Micro 兼容排针命名，但底层是 RP2040。当前排障已确认异常严格集中在 `F4~F7 → GP26~GP29` 这组 ADC 复用脚上。对于 Ferris Sweep 的 direct-pins 扫描，这组脚是否能稳定作为普通数字输入，取决于板级电气设计与初始化状态。

当前更像是 **GP26-GP29 这组脚在该兼容板上的板级电气/复用约束问题**，而不太像 QMK direct pin 逻辑本身的问题。

`F4~F7 → GP26~GP29` 的映射对当前这块板的 `rp2040_ce` 适配是已验证的；但这并不自动推出该板对 GP26-GP29 的板级电气处理适合 direct-pins。若启动阶段这些脚没有被稳定配置为适合键盘扫描的数字输入状态，或被板载外部电路拖住，就可能出现浮空误读。

---

## 期望

1. 找到 ghost keys 的真正根因
2. GP26-29 能作为普通数字输入正常工作（内部上拉，低电平有效）
3. 最终固件无需外部硬件修改（如加上拉电阻），但如果确认是板级硬件问题，也接受硬件方案

---

## 开放问题

1. `rp2040_ce` converter 的引脚映射是否真的将 F4-F7 映射到 GP26-29？有没有可能映射到了其他 GPIO？（很多国产板的 pinout 可能与标准不符）
2. 这块国产 RP2040 Pro Micro 兼容板上，GP26-29 是否有板载下拉电阻或其他外部电路（如 ADC 参考电压分压器、LED、分压电路）？没有原理图无法确认。
3. vial-qmk 的 ChibiOS 版本中，`palSetLineMode(pin, PAL_MODE_INPUT_PULLUP)` 对 RP2040 的实现是否真的会写 PADS 寄存器的 IE bit？
4. QMK 的 DIRECT_PINS 扫描是低电平有效还是高电平有效？`MATRIX_INPUT_PRESSED_STATE` 的默认值是什么？
5. `board_init()` 这个 hook 在 vial-qmk 中是否真的被调用？
6. 寄存器写入是否真的生效了？目前无法读取寄存器实际值（诊断输出被 ghost keys 淹没）
7. 是否有其他使用 RP2040 + GP26-29 作为键盘矩阵/direct pin 引脚的成功案例？

---

## 引脚对照表

### 已确认（实物丝印 + 原理图交叉验证）

主控实物上，GP20-GP23 位于主控右侧最下方。QWERT 五个键接的是 GP7, GP26(ADC0), GP27(ADC1), GP28(ADC2), GP29(ADC3)。

| Pro Micro 别名 | RP2040 GPIO | 确认状态 | 备注 |
|---|---:|---|---|
| E6 | GP7 | ✅ 实物确认 | Q 键，工作正常 |
| F7 | GP26 | ✅ 实物确认 | W 键，ADC0，**异常** |
| F6 | GP27 | ✅ 实物确认 | E 键，ADC1，**异常** |
| F5 | GP28 | ✅ 实物确认 | R 键，ADC2，**异常** |
| F4 | GP29 | ✅ 实物确认 | T 键，ADC3，**异常** |
| D0-D8 | GP0-GP8 | ✅ 原理图覆盖 | 工作正常 |
| D9-D10 | GP9-GP10 | ✅ 原理图覆盖 | 工作正常 |
| D14-D16 | GP14-GP16 | ✅ 原理图覆盖 | 工作正常 |
| D18-D21 | GP18-GP21 | ✅ 原理图覆盖 | 工作正常 |

### 工作假设（尚未硬证实）

| Pro Micro 别名 | RP2040 GPIO | 确认状态 | 物理位置 |
|---|---:|---|---|
| B1 | GP20 | ⚠️ 假设 | 主控右侧下方 |
| B3 | GP21 | ⚠️ 假设 | 主控右侧下方 |
| B2 | GP22 | ⚠️ 假设 | 主控右侧下方 |
| B6 | GP23 | ⚠️ 假设 | 主控右侧下方 |

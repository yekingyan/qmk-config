# Ferris Sweep + RP2040 Pro Micro 兼容主控：Ghost Keys 问题求助

## 一句话描述

Ferris Sweep (34键 direct pin 分体键盘) 使用 RP2040 Pro Micro 兼容主控，刷入 QMK (vial-qmk) 固件后，GP26-29 (Pro Micro 别名 F4-F7) 对应的 4 个按键持续自动触发，输出 `werttttt`。不接 PCB 纯主控裸板也复现。已尝试多种软件修复方案均无效。

---

## 项目背景

### 键盘

- **型号**：Ferris Sweep (又名 Cradio)，34 键分体键盘
- **矩阵方式**：**DIRECT_PINS**（无二极管，每个按键直连一个 GPIO）
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

### 引脚映射关系

Sweep 的 DIRECT_PINS 定义（来自 vial-qmk 上游）：

```c
// 左手
#define DIRECT_PINS { \
    { E6, F7, F6, F5, F4 }, \
    { B1, B3, B2, B6, D3 }, \
    { D1, D0, D4, C6, D7 }, \
    { B4, B5, NO_PIN, NO_PIN, NO_PIN } \
}
```

通过 `rp2040_ce` converter，Pro Micro 别名映射到 RP2040 GPIO：

| 矩阵位置 | 按键 | Pro Micro 别名 | RP2040 GPIO | 特殊性 |
|-----------|------|----------------|-------------|--------|
| (0,1) | W | F7 | GP26 | **ADC0** |
| (0,2) | E | F6 | GP27 | **ADC1** |
| (0,3) | R | F5 | GP28 | **ADC2** |
| (0,4) | T | F4 | GP29 | **ADC3** |

**GP26-29 是 RP2040 的 ADC 专用复用引脚。**

### 关键观测

- **不插 PCB 也复现** → 排除 PCB 走线、焊接、短路问题
- **等几秒才出现** → 不是上电瞬间的浮空毛刺，是 QMK matrix scan 循环开始后才触发
- **持续输出 `t`** → GP29 (F4) 持续被读为"按下"状态（低电平）
- **`wert` 全部触发** → GP26-29 四个引脚全部异常
- **其他引脚正常** → 只有 GP26-29 这四个 ADC 复用引脚有问题

---

## RP2040 GP26-29 的硬件特殊性

根据 RP2040 datasheet：

1. **硬件复位后**，GP26-29 的 `PADS_BANK0` 寄存器默认值为 `0x1F`
   - 其中 **IE (Input Enable, bit 6) = 0**，即输入禁用
   - 其他 GPIO (GP0-GP25) 的默认值为 `0x56`，IE = 1
2. 这是因为 GP26-29 设计为 ADC 输入，硬件默认关闭数字输入以减少噪声
3. 要将 GP26-29 用作普通数字 GPIO，必须手动设置：
   - `FUNCSEL = 5` (SIO/GPIO 模式)
   - `IE = 1` (启用输入)
   - `PUE = 1` (启用上拉，对于 direct pin 键盘)

---

## 已尝试的方案（全部无效）

### 方案 1：mcuconf.h 禁用 ADC 外设

```c
// mcuconf.h
#include_next <mcuconf.h>
#undef RP_ADC_USE_ADC1
#define RP_ADC_USE_ADC1 FALSE
```

**目的**：阻止 ChibiOS ADC 驱动在 HAL 初始化阶段接管 GP26-29。

**结果**：❌ ghost keys 依旧

### 方案 2：halconf.h 关闭 ADC 驱动编译

```c
// halconf.h
#include_next <halconf.h>
#undef HAL_USE_ADC
#define HAL_USE_ADC FALSE
```

**目的**：从编译层面完全移除 ADC 驱动代码。

**结果**：❌ ghost keys 依旧

### 方案 3：board_init() 寄存器级修复

```c
#define PADS_BANK0_GPIO(n) (*(volatile uint32_t *)(PADS_BANK0_BASE + 0x04 + (n) * 4))
#define IO_BANK0_GPIO_CTRL(n) (*(volatile uint32_t *)(IO_BANK0_BASE + 0x04 + (n) * 8))

void board_init(void)  (uint8_t pin = 26; pin <= 29; pin++) {
        IO_BANK0_GPIO_CTRL(pin) = 5;  // FUNCSEL = SIO
        PADS_BANK0_GPIO(pin) = (1 << 6) | (1 << 3) | (1 << 1);  // IE + PUE + SCHMITT
    }
}
```

**目的**：在 ChibiOS `halInit()` 内部（`boardInit()` hook）直接写 RP2040 寄存器，强制启用 GP26-29 的数字输入 + 上拉。

**结果**：❌ ghost keys 依旧

### 方案 4：keyboard_post_init_user() 二次修复

在 QMK 初始化链的最后一步再次写寄存器。

**结果**：❌ ghost keys 依旧

### 方案 5：三层全部叠加

mcuconf.h + halconf.h + board_init() + keyboard_post_init_user() 全部保留。

**结果**：❌ ghost keys 依旧

### 诊断尝试

| 诊断方法 | 结果 |
|----------|------|
| `keyboard_post_init_user` 中 `wait_ms(3000)` | ✅ 延迟可观测，代码确实在执行 |
| `keyboard_post_init_user` 中 `send_string("GPIOFIX")` | ❌ 未观测到输出（USB HID 在此阶段未就绪） |
| `matrix_scan_user` 首次执行时 `send_string("DIAG ...")` | ❌ 被 ghost keys 输出淹没，无法辨认 |
| 寄存器值读取 + 输出 | ❌ 同上，被淹没 |

### 已确认的事实

1. **自定义代码确实被编译进固件**：`wait_ms()` 延迟可观测
2. **`board_init()` 和 `keyboard_post_init_user()` 都在执行**
3. **寄存器写入操作在执行**（但无法确认写入后的实际值，因为诊断输出被淹没）
4. **无法通过 `send_string` 获取诊断信息**：ghost keys 输出速率太高，诊断被淹没

---

## 全链路源码审计发现

对 QMK + ChibiOS + Pico SDK 的初始化链做了完整审计：

### ChibiOS 层

- `RP_PICO_RP2040/board.c`：`__early_init()` 和 `boardInit()` 均为空函数
-_pal_lld_init()`：只做 `unreset IO_BANK0` 和 `unreset PADS_BANK0`，不碰任何引脚的 PADS 寄存器
- `palSetLineMode(pin, PAL_MODE_INPUT_PULLUP)` 的实现会写 PADS 寄存器（含 IE=1）和 IO_BANK0 CTRL（FUNCSEL=SIO）

### QMK 层

- `matrix_init_pins()`（DIRECT_PINS 模式）对每个引脚调用 `gpio_set_pin_input_high(pin)` → `palSetLineMode(pin, PAL_MODE_INPUT_PULLUP)`
- 理论上 `matrix_init_pins()` 应该能正确配置 GP26-29

### 疑点

- `palSetLineMode` 理论上会正确设置 IE bit，但实际效果未知（无法读取寄存器值）
- 不清楚 vial-qmk 使用的 ChibiOS 版本是否与 QMK 主线一致
- 不清楚 `rp2040_ce` converter 的引脚映射是否真的将 F4-F7 映射到 GP26-29（虽然文档和社区信息都指向这个映射）

---

## 当前代码文件

### rules.mk

```makefile
VIA_ENABLE = yes
VIAL_ENABLyes
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
    for (uint8_t pin = 26; pin <= 29; pin++) { IO_BANK0_GPIO_CTRL(pin) = 5;
        PADS_BANK0_GPIO(pin) = (1 << 6) | (1 << 3) | (1 << 1);
    }
}
```

---

## 编译流程

GitHub Actions 云编译：

1. Checkout 用户仓库 `yekingyan/qmk-config`
2. Checkout `vial-kb/vial-qmk` (ref: `vial`, with submodules)
3. 将用户 keymap 文件复制到 `vial-qmk/keyboards/ferris/sweep/keymaps/yekingyan/`
4. `make ferris/sweep:yekingyan`
5. 产出 `.uf2` 文件

刷机方式：双击 reset 进入 UF2 bootloader，拖入 `.uf2` 文件。

---

## 提交历史（按时间倒序）

```
8cfcd4c docs: 记录 ghost keys 验证历程与待排查方向
df271df diag: 诊断移到 matrix_scan_user 首USB 此时必定 ready
4077d32 diag等待延长到 3 秒，确保 send_string 能正常发送
0ef1566 diag: 先输出寄存器诊断再等10秒，避免被ghost keys淹没
281092e diag: Base 层 W/E/R/T 临时改为 1/2/3/4，验证 keymap 是否真的被编译进固件
64580c0 diag: 读取 GP26-29 寄存器修复前后实际值，3秒延迟后输出诊断
e223220 diag: board_init + keyboard_post_init_user 双保险修复 GP26-29，加诊断输出 GPIOFIX
a662878 fix: 移除 PADS_BANK0_BASE/IO_BANK0_BASE 重复定义，复用 pico-sdk 宏
b50a75d docs: 添加 RP2040 Pro Micro 主控规格与引脚映射文档
e367d81 docs: 更新 plan.md 记录 GP26-29 ghost keys 根因分析与验证路线
0ba9c0a fix: board_init 强制启用 GP26-29 IE bit + FUNCSEL=SIO
72ab42a fix: halconf.h 先 include_next 再 undef，修复重定义冲突
1045e72 fix: halconf.h 关闭 HAL_USE_ADC 避免 ADC 驱动编译错误
e2b3244 fix: 通过 mcuconf.h 禁用 ADC 外设解决 GP26-29 ghost keys
0f1db19 fix: 直接写 RP2040 寄存器强制关闭 ADC 功能，override matrix_init_pins
b03af1c diag: 启动后自动输出所有 GPIO 状态，排查引脚映射
c14cded fix: 用 converter 引脚名 F4-F7 替代 GP26-29，修复编译错误
04fff10 fix: 用 QMK GPIO 抽象层修复 ADC 引脚上拉，移除 Pico SDK 依赖
3470589 fix: RP2040 ADC 引脚 GP26-29 强制切 GPIO 模式+上拉，修复 ghost keys
```

---

## 期望

1. 找到 ghost keys 的真正根因
2. GP26-29 能作为普通数字输入正常工作（内部上拉，低电平有效）
3. 最终固件无需外部硬件修改（如加上拉电阻）

---

## 开放问题

以下是我们目前无法确认、可能与问题相关的方向：

1. `rp2040_ce` converter 的引脚映射是否真的将 F4-F7 映射到 GP26-29？有没有可能映射到了其他 GPIO？
2. 这块国产 RP2040 Pro Micro 兼容板上，GP26- 是否有板载下拉电阻或其他外部电路（如 ADC 参考电压分压器）？没有原理图无法确认。
3. vial-qmk 的 ChibiOS 版本中，`palSetLineMode(pin, PAL_MODE_INPUT_PULLUP)` 对 RP2040 的实现是否真的会写 PADS 寄存器的 IE bit？
4. QMK 的 DIRECT_PINS 扫描是低电平有效还是高电平有效？`MATRIX_INPUT_PRESSED_STATE` 的默认值是什么？
5. `board_init()` 这个 hook 在 vial-qmk 中是否真的被调用？（ChibiOS 的 `boardInit()` 在不同版本中行为可能不同）
6. 寄存器写入是否真的生效了？目前无法读取寄存器实际值（诊断输出被 ghost keys 淹没）
7. 是否有其他使用 RP2040 + GP26-29 作为键盘矩阵引脚的成功案例？

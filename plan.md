# QMK 项目驾驶舱

## 活跃项目：Dolphin52

> 52 键分体键盘，YD-RP2040 主控，Direct Pin，Vial QMK。

详见 `docs/dolphin52-notes.md`

### 状态

- [x] QMK 固件初始化（keyboard definition + direct pin 配置）
- [x] 键位移植（Sweep 7 层核心 + 外围传统键位）
- [x] Vial 支持
- [x] GitHub Actions 云编译
- [ ] 刷机验证

### 文件结构

```
keyboards/dolphin52/
├── keyboard.json          # 键盘元信息 + 52键布局定义
├── config.h               # RP2040 双击复位 + 串口 + Direct Pin 矩阵
├── rules.mk               # GENERIC_RP_RP2040 board + PIO serial driver
└── keymaps/vial/
    ├── keymap.c           # 7层键位 (核心Sweep + 外围传统)
    ├── config.h           # Vial + Tap-Hold + OSM + Mouse 配置
    ├── rules.mk           # Vial/VIA + Combo + Caps Word
    └── vial.json          # Vial GUI 布局描述
```

---

## 归档项目：Ferris Sweep（RP2040 Pro Micro）

> 2026-05-22 归档。原因：RP2040 Pro Micro 兼容主控的 GP26-29 ghost keys 问题无法解决，direct pin 方案走不通。

<details>
<summary>归档详情</summary>

### 概要

Ferris Sweep 34 键 Vial 固件，从 ZMK 移植，云编译。

- 主控：RP2040 Pro Micro 兼容主控（商家标题：树莓派迷你开发板ProMicro RP2040兼容Helios OxB2）
- 源码：`keyboards/ferris/sweep/keymaps/yekingyan/`
- 主控说明：`docs/rp2040-pro-micro-controller-notes.md`

### 已完成

- [x] ZMK 7 层键位完整移植到 QMK
- [x] Swapper（Alt-Tab 宏）自定义实现
- [x] Caps Word 移植
- [x] Vial 支持（GUI 改键）
- [x] GitHub Actions 云编译通过
- [x] 左手主控刷入测试

### 阻塞原因：GP26-29 ghost keys

刷入固件后自动输出 `werttttt`，接不接 PCB 均复现。

**根因**：RP2040 的 GP26-29 是 ADC 复用引脚，该主控板载可能有下拉电路（ADC 参考电压分压器），导致这些引脚始终被读为低电平。软件层面（关 ADC 外设、寄存器 hack）均无法修复。

**尝试过的修复**：
- mcuconf.h 关 ADC 外设
- halconf.h 关 ADC 驱动
- board_init() 寄存器 hack 强制 IE/PUE/SCHMITT
- keyboard_post_init_user 二次修复
- 多种诊断手段（send_string、延迟、matrix_scan 首次执行）

全部失败。结论：该主控硬件层面不适合将 GP26-29 用作 direct pin 输入。

### 经验教训

1. RP2040 的 GP26-29 做键盘矩阵/direct pin 需要硬件配合（外部上拉或 PCB 设计避开）
2. 社区大多数键盘设计避开这些引脚是有原因的
3. Pro Micro 兼容主控的 converter 方案无法绕过硬件引脚限制

</details>

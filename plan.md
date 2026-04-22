# QMK Sweep — 项目驾驶舱

> Ferris Sweep 34 键 Vial 固件，从 ZMK 移植，云编译。

## 现状

- 固件：Vial QMK，7 层键位（Base/Nav/Num/Sym/Mouse/Fun/Media）
- 主控：Pro Micro RP2040（国产 Helios/Splinky 兼容），converter `sparkfun_pm2040`
- 编译：GitHub Actions 云编译（clone `vial-kb/vial-qmk` ref `vial` → copy keymap → make）
- 源码：`keyboards/ferris/sweep/keymaps/yekingyan/`
- 仓库：`yekingyan/qmk-config`

### 文件清单

| 文件 | 用途 |
|------|------|
| `keymap.c` | 7 层键位 + Swapper + Caps Word + RP2040 ADC 修复 |
| `config.h` | Vial UID/解锁 combo + tap-hold + 鼠标速度 |
| `rules.mk` | 功能开关 + converter 选择 |
| `vial.json` | Sweep 矩阵布局（Vial GUI 用） |
| `.github/workflows/build_binaries.yaml` | 云编译 workflow |

## 进度

- [x] ZMK 7 层键位完整移植到 QMK
- [x] Swapper（Alt-Tab 宏）自定义实现
- [x] Caps Word 移植
- [x] Vial 支持（GUI 改键）
- [x] GitHub Actions 云编译通过
- [x] 左手主控刷入测试
- [ ] **RP2040 ADC ghost keys 修复验证中**（GP26-29 强制 GPIO 模式 + 上拉）
- [ ] Vial GUI 配置 5 个 combo
- [ ] 右手焊接 + 刷机
- [ ] 鼠标加速曲线实测微调

## 期望

- 左右手均正常工作，无 ghost keys
- Vial GUI 可实时改键，方便给别人用
- 键位体验与 ZMK 版一致（Callum OSM + Vim Nav 核心）
- 一个 `.uf2` 左右手通用

## 问题

### 🔴 RP2040 ADC 引脚 ghost keys（排查中）

刷入固件后自动输出 `werttttt`，接不接 PCB 均复现。

**根因分析**：Sweep 使用 direct pin 方案，左手第一行 W/E/R/T 映射到 Pro Micro 的 F7/F6/F5/F4，经 converter 对应 RP2040 的 GP26/27/28/29。这四个是 ADC 复用引脚，上电默认进 ADC 模式，QMK 设置的内部上拉不生效，引脚浮空读到低电平 → 误判为按下。

**修复方案**：`keyboard_post_init_user()` 中用 Pico SDK `gpio_init()` 强制切回 GPIO 模式并启用上拉。已提交 `3470589`，等待验证。

**备选方案**：若软件修复无效，可能需要在 PCB 上为 GP26-29 加外部上拉电阻（10kΩ 到 VCC）。

### 🟡 Converter 选择不确定

国产 RP2040 Pro Micro 标称"兼容 Helios 0xB2"，实测 `helios` 和 `rp2040_ce` 引脚映射完全一致（helios 是 rp2040_ce 的别名）。当前使用 `sparkfun_pm2040`，引脚映射与 rp2040_ce 仅 LED 引脚不同（GP17/16 vs GP12/13），功能引脚完全一致。待 ghost keys 修复后确认最终 converter。

### 🟢 Combo 未配置

5 个 combo（S+D→ESC, F+J→CW_TOGG, J+K→LSFT, 左双拇指→FUN, 右双拇指→MEDIA）由 Vial EEPROM 管理，刷完固件后在 Vial GUI 中配置即可，不阻塞开发。

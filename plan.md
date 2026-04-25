# QMK Sweep — 项目驾驶舱

> Ferris Sweep 34 键 Vial 固件，从 ZMK 移植，云编译。

## 现状

- 固件：Vial QMK，7 层键位（Base/Nav/Num/Sym/Mouse/Fun/Media）
- 主控：`RP2040` + `Pro Micro` 兼容主控（当前使用 `4MB` 闪存）；商家标题为 `树莓派迷你开发板ProMicro RP2040兼容Helios OxB2 MicroPython`
- 主控说明：见 `docs/rp2040-pro-micro-controller-notes.md`
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
| `docs/rp2040-pro-micro-controller-notes.md` | 主控规格、引脚映射与排障口径 |

## 进度

- [x] ZMK 7 层键位完整移植到 QMK
- [x] Swapper（Alt-Tab 宏）自定义实现
- [x] Caps Word 移植
- [x] Vial 支持（GUI 改键）
- [x] GitHub Actions 云编译通过
- [x] 左手主控刷入测试
- [ ] **RP2040 GP26-29 ghost keys 修复验证中**（根因已确认，三层修复已提交，方案 A 待刷机验证）
- [ ] Vial GUI 配置 5 个 combo
- [ ] 右手焊接 + 刷机
- [ ] 鼠标加速曲线实测微调

## 期望

- 左右手均正常工作，无 ghost keys
- Vial GUI 可实时改键，方便给别人用
- 键位体验与 ZMK 版一致（Callum OSM + Vim Nav 核心）
- 一个 `.uf2` 左右手通用

## 问题

### 🔴 RP2040 GP26-29 ghost keys

刷入固件后自动输出 `werttttt`，接不接 PCB 均复现。

#### 根因（已确认）

- `F7→GP26`, `F6→GP27`, `F5→GP28`, `F4→GP29` 是 RP2040 ADC 复用引脚
- 硬件复位后 `PADS_BANK0` 默认值 `0x1F`，IE bit (bit6) = 0，输入禁用
- QMK 官方 `GENERIC_PROMICRO_RP2040` 的 `mcuconf.h` 默认 `RP_ADC_USE_ADC1 = TRUE`
- ChibiOS ADC 驱动在 HAL 初始化阶段接管 GP26-29 为模拟输入，与后续 matrix init 存在时序竞争
- ChibiOS `board.c` 的 `__early_init()` / `boardInit()` 均为空，PAL 驱动 `__pal_lld_init()` 不碰引脚 PADS 寄存器
- QMK 官方 Sweep 源码无 RP2040 特殊处理（原版用 ATmega32U4）
- 社区大多数键盘设计避开 GP26-29 做矩阵引脚，无成熟先例可参考

#### 全链路源码审计结论（2026-04-25）

ChibiOS `palSetLineMode(pin, PAL_MODE_INPUT_PULLUP)` 会正确写 PADS 寄存器（含 IE=1），
因此 QMK `matrix_init_pins()` 理论上能正确配置 GP26-29——前提是 ADC 驱动不先接管。

#### 当前修复（三层防御，commit `0ba9c0a`）

| 层 | 文件 | 做了什么 | 必要性 |
|----|------|----------|--------|
| 1 | `mcuconf.h` | `RP_ADC_USE_ADC1 FALSE` | ✅ 必须：从源头关掉 ADC 外设 |
| 2 | `halconf.h` | `HAL_USE_ADC FALSE` | ✅ 推荐：关掉 ADC 驱动编译 |
| 3 | `keymap.c` → `board_init()` | 寄存器 hack 强制 IE/PUE/SCHMITT | ⚠️ 双保险：理论上前两层已足够 |

#### 验证路线

按顺序逐步精简，每步刷机验证：

1. **方案 A（当前状态）**：三层全保留，先确认当前固件是否已解决 ghost keys → 待验证
2. **方案 B**：去掉第 3 层 `board_init()` hack，只保留 `mcuconf.h` + `halconf.h` 关 ADC → 验证 QMK matrix init 的 `palSetLineMode` 是否足够
3. **方案 C**：只保留 `mcuconf.h` 的 `RP_ADC_USE_ADC1 FALSE`，去掉 `halconf.h` 覆盖 → 最精简
4. **硬件兜底**：若以上均不稳定，GP26-29 加 10kΩ 外部上拉电阻

当前状态：**方案 A 已验证失败，诊断阶段**

#### 验证记录（2026-04-25）

| 提交 | 方案 | 结果 |
|------|------|------|
| `a662878` | 三层全保留（mcuconf+halconf+board_init 寄存器 hack） | ❌ ghost keys 依旧 `werttttt` |
| `e223220` | + `keyboard_post_init_user` 二次修复 + `send_string("GPIOFIX")` 诊断 | ❌ ghost keys 依旧，GPIOFIX 未观测到（被淹没） |
| `64580c0` | + 寄存器前后值采集 + 3s 延迟后输出诊断 | ❌ 有 3s 延迟（代码在跑），但诊断字符串未输出（USB 未 ready） |
| `4077d32` | USB 等待延长到 3s | ❌ 同上，send_string 在 post_init 阶段无法发送 |
| `df271df` | 诊断移到 `matrix_scan_user` 首次执行（USB 必定 ready） | ❌ 仍只看到 ghost keys，诊断被淹没 |

#### 已确认的事实

1. **代码确实在执行**：`wait_ms()` 延迟可观测（3s/10s 延迟均能感知）
2. **send_string 在 post_init 阶段不工作**：USB HID 在此阶段未就绪
3. **send_string 在 matrix_scan 阶段被 ghost keys 淹没**：ghost keys 输出速率太高
4. **不插 PCB 纯主控裸板也复现**：排除 PCB 走线/焊接问题
5. **插上 USB 等几秒才出现**：是 QMK matrix scan 触发，非上电瞬间浮空
6. **持续输出 `t`**：GP29(F4) 持续被读为低电平
7. **寄存器 hack 写入后 ghost keys 依旧**：根因可能不是 IE bit

#### 待排查方向

- a) `send_string` 诊断被淹没 → 改用 QMK Console (`CONSOLE_ENABLE`) 通过 `dprintf` 输出，用 `qmk console` 或 `hid_listen` 读取
- b) converter 引脚映射验证 → 确认 `rp2040_ce` 的 F4-F7 是否真的映射到 GP26-29
- c) 主控硬件问题 → GP26-29 可能有板载下拉电路（ADC 参考电压分压器）
- d) DIRECT_PINS 极性 → 确认 Sweep 的 direct pin 是低电平有效还是高电平有效

### 🟡 Converter 口径待收敛

卖家标题明确使用“兼容 `Helios 0xB2`”口径；当前仓库实际配置以代码为准（见 `rules.mk`）。

现阶段结论：

当前使用 `CONVERT_TO = rp2040_ce`。converter 选型不影响 ghost keys 根因（ADC 引脚 IE 位问题），待 ghost keys 修复验证通过后再收敛。

### 🟢 Combo 未配置

5 个 combo（S+D→ESC, F+J→CW_TOGG, J+K→LSFT, 左双拇指→FUN, 右双拇指→MEDIA）由 Vial EEPROM 管理，刷完固件后在 Vial GUI 中配置即可，不阻塞开发。

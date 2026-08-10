# QMK 项目驾驶舱

## 活跃项目：Dolphin54

> 54 键分体键盘，YD-RP2040 主控，Direct Pin，Vial QMK。
> 相比 Dolphin52 左右各多一个最外侧拇指键（均连接 GP23 引脚）。

### 状态

- [x] QMK 固件初始化（Dolphin54 54键配置与布局）
- [x] 键位移植（大拇指最外侧分配左手 LALT，右手 RGUI）
- [x] Vial 支持（Vial.json 渲染 6 拇指）
- [x] GitHub Actions 云编译
- [x] 键位修改：左侧 Esc 下方改为 `-` (KC_MINS)，右侧最外列按键帽高度重排（由上至下：`+` KC_EQL、`[` KC_LBRC、`]` KC_RBRC、`"` KC_QUOT）
- [x] 拇指切层修改：移除单按键 LT 切层，在代码中通过 space_pressed 等状态自定义实现 Space+Tab 秒进 FUN 层，Enter+Backspace 秒进 MEDIA 层。彻底移除 `update_tri_layer_state`，完美兼容 Vial UI 内将其设为纯按键或 `LT()` 混搭的情况。
- [x] Console 日志与 Debug 调试配置（已注释关闭：`# CONSOLE_ENABLE = yes` 及 `debug_*` 注释保留，需要时随时取消注释）
- [x] 显式关闭 RGB 功能（`RGBLIGHT_ENABLE = no` 及 `RGB_MATRIX_ENABLE = no`，防止向 GP23 引脚输出灯光控制信号）
- [x] USB 唤醒防假死配置（加入 `#define NO_SUSPEND_POWER_DOWN` 及 `#define NO_USB_STARTUP_CHECK` 彻底解决笔记本 5V 持续供电导致的唤醒死机问题）
- [x] 主手卡死自恢复（`dolphin54.c` 启用 RP2040 硬件看门狗，4s 超时，在 `housekeeping_task_kb` 喂狗。QMK 的 `SPLIT_WATCHDOG_ENABLE` 只保护从手）
- [x] 分体串口死锁根因修复（`serial_vendor.c` 本地覆盖，给两处 `osalSysLock()` 内的无超时忙等加时间上界；CI 有上游漂移与覆盖失效两道守卫）
- [ ] 刷机验证（**本次两边都要刷**：看门狗与串口驱动在主从两侧都生效，不属于「只改键位」）
- [ ] **待定因：使用中偶发整机无响应**（详见下方「未结案：使用中偶发卡死」）

### 未结案：使用中偶发卡死

**现象**（2026-08-10）：正常使用中忽然整机无响应，键盘 HID 和 Vial（raw HID）**同时**失效，只有热插拔 USB 能恢复。存活时长通常 7~8 小时，有时几天，偶尔短到 1.5 小时以内 —— 概率性触发，非周期性。

**已确认的事实**（Windows 侧 PnP 查询 + vial-qmk 源码核对）：

- 主机侧完全正常：`VID_594B/PID_D054` 三个接口 Status=OK、ProblemCode 空、电源状态 D0，卡死期间**没有重新枚举**（`LastArrivalDate` 不变），系统事件日志零条 USB/HID 错误。→ 不是线材/驱动/选择性挂起/重启循环。
- 两条 HID 通路同时死 + USB 枚举仍在（由中断维持）→ 主循环侧硬停摆。
- **主手没有任何看门狗**：`quantum/split_common/split_util.c` 的 `split_watchdog_task()` 条件是
  `if (!split_watchdog_done && !is_keyboard_master())`，`SPLIT_WATCHDOG_ENABLE` 明确只作用于从手。
  这解释了为什么必须手动拔插才能恢复。

**根因首选：PIO 半双工串口驱动里的无界忙等**

`platforms/chibios/drivers/vendor/RP/RP2040/serial_vendor.c` 有两处 `osalSysLock()`
（内核级屏蔽中断）之内的**无超时**循环：

1. `enter_rx_state()`：`while (!pio_sm_is_tx_fifo_empty(pio, tx_state_machine)) {}`
   —— 每次 `serial_transport_send()` 结尾都会走到。TX 状态机若卡住不排空 FIFO 即永久死锁。
2. `serial_transport_driver_clear()`：`while (!pio_sm_is_rx_fifo_empty(...)) { pio_sm_clear_fifos(...); }`
   —— RX 线若被持续噪声/常低驱动，PIO 会不断 push，该循环也可能永不退出。

两者都能精确复现观察到的签名：主循环停摆 + 中断被屏蔽 → 键盘 HID 与 raw HID 同时死 →
主机侧仍显示已枚举、无任何错误 → 只有上电复位可救；且触发条件是罕见时序/噪声，符合小时到天的 MTBF。

**上游 QMK master 该文件代码完全相同**，即所有 RP2040 半双工分体键盘共有的潜在缺陷，无现成补丁可摘。

**已排除的假设**：

- ChibiOS 32 位 @1MHz 计时器回绕（71.58 分钟）：回绕是确定性的，与「7~8 小时 / 几天」的实际分布矛盾。
  另外 QMK 的 `timer_read32()` 本身已有回绕补偿（`platforms/chibios/timer.c` 的 `OVERFLOW_ADJUST_TICKS`）。
- 开机主从判定竞态：那种情况从插上就不工作，与「用着用着才死」矛盾。

**已做的处置**：加 RP2040 硬件看门狗（见上方状态项）。它是纯硬件计数器，不受 `osalSysLock()`
屏蔽中断影响，所以能真正救上述死锁；死锁使主循环停止 → 停止喂狗 → 4s 后自动复位。
主从两侧都覆盖（从手的死锁发生在 `SlaveThread`，但它持内核锁同样会冻住主循环）。

**根因修复：本地覆盖上游驱动（已实施）**

覆盖机制：`serial_vendor.c` 通过 `QUANTUM_LIB_SRC += serial_$(SERIAL_DRIVER).c` 以**裸文件名**
加入编译，靠 VPATH 解析；`builddefs/build_keyboard.mk` 中顺序为
`KEYMAP_PATH` → `USER_PATH` → `KEYBOARD_PATHS` → `COMMON_VPATH`，键盘目录**先于**平台驱动目录。
所以 `keyboards/dolphin5x/serial_vendor.c` 会覆盖平台版本。
**依赖仓库 vial-qmk 完全不需要修改，不用提 MR。**

改动内容：给那两处忙等加时间上界（`osalOsGetSystemTimeX()` / `osalTimeDiffX()`，X 后缀可在
临界区内安全调用；上界 = 8×11bit/230400bps 的 10 倍 ≈ 3819µs）。超时后放弃本次分体事务，
协议层已能容忍单次失败。已在反汇编中确认两处的超时常量 `0x00000eeb`(3819) 与条件出口均存在。

**上游漂移的两道 CI 守卫**（都已本地实跑验证通过与失败两条路径）：

1. `.github/vendored-upstream.sha256` + `sha256sum -c`：校验上游原文是否仍是基线版本
   （基线 commit `dd43959ae5c08d8a28d38a1acf7b04e86b14a344`，
   sha256 `81c1be5e...db8f`）。上游一改动就让构建失败，逼迫同步而不是悄悄用旧版。
   失败时会打印上游新版与本仓库副本的 diff。
2. 构建后 grep 编译日志，确认编译的是 `keyboards/<kb>/serial_vendor.c` 而非平台目录那份。
   若上游改变了该文件加入 SRC 的方式，覆盖会静默失效、上界随之丢失，此守卫让它显式报错。

另外 artifact 上传加了 `if-no-files-found: error`，产物缺失即失败。

**配置侧备选降概率手段**（未应用）：调低分体串口速率 `#define SELECT_SOFT_SERIAL_SPEED 2`
（230400 → 115200）。给 PIO 更多时序余量，但只降概率不消除死锁，且无证据表明异常与速率相关。

**看门狗兼作判别器**：若卡死后能自动恢复（约 4s，`LastArrivalDate` 会更新）→ 主循环确实停摆，
支持串口死锁假设；若仍需手动拔插 → 主循环是活的，问题在 USB 发送通路，需换方向排查。

**重要耦合：不要去掉 `NO_USB_STARTUP_CHECK`**

`tmk_core/protocol/chibios/chibios.c` 的 `protocol_pre_task()` 里，主机休眠时的阻塞循环
（`while (USB_DRIVER.state == USB_SUSPENDED)`）被 `#if !defined(NO_USB_STARTUP_CHECK)` 包着，
且循环内**不调用** `housekeeping_task()`。当前定义了该宏所以循环被编译掉、休眠期间照常喂狗。
一旦去掉这个宏，硬件看门狗就会在主机休眠时每 4 秒复位一次。两者互斥。

---

## 阶段性升级项目：Dolphin52

> 52 键分体键盘，YD-RP2040 主控，Direct Pin，Vial QMK。
> **2026-07-16 记录**：已成功验证 GP23 测试引脚可用。项目已正式分化出 54 键版本的 **Dolphin54**，Dolphin52 本身已撤销测试配置，恢复为纯净的 52 键固件。

### 状态

- [x] QMK 固件初始化（keyboard definition + direct pin 配置）
- [x] 键位移植（Sweep 7 层核心 + 外围传统键位）
- [x] Vial 支持
- [x] GitHub Actions 云编译
- [ ] 刷机验证

### 刷机须知

- **只改键位**：只刷主手（插 USB 那半），从手无需重刷
- **改通信协议 / QMK 大版本升级**：两边都刷

### 键位需求（对齐 zmk-config 设计）

> 34 键核心区必须与 `~/projects/zmk-config/docs/keymap-design.md` 完全一致。

- **Nav 层右手 Q 行**：`C(←) C(D) C(U) C(→) DEL`（按词跳跃 / Vim 翻页），使用标准 QMK `C()` 宏
- **Nav-Mac 层**：Mac 对应键使用标准 `G()`/`A()` 宏（⌘剪贴板 + ⌥词跳）
- **OSM 粘滞修饰**：QMK 内置 `OSM()` 在 LT 激活层上有 bug（独立按键发送），改用自定义 `SK_LGUI/LALT/LCTL/LSFT`
  - 行为对齐 ZMK `&skn`：chain 累加（多个修饰可叠加），1s 超时释放，重复按只刷新计时不 toggle-off
  - 左手 Nav 层 A/S/D/F = Win/Alt/Ctrl/Shift 粘滞；右手 Sym/Mouse/Media 层镜像区同样
- **Combo**：S+D=Esc、J+K=LShift、F+J=CapsWord，通过 `eeconfig_init_user()` 写入 Vial EEPROM 默认值（F+J 跨手可能不触发，备用 Nav 层 G 位 CW_TOGG）
- **Bootloader**：_FUN 层左上和右上都是 `QK_BOOTLOADER`（vial-qmk 兼容名），仅左手也能进刷机
- **Tapping 配置**：`PERMISSIVE_HOLD` + `TAPPING_TERM 200` + `QUICK_TAP_TERM 150`，全局启用，稳定 LT 判定
- **层内空位**：非 BASE 层未定义位使用 `KC_NO`（按了无反应），不透传
- **拇指键**：激活当前层的拇指键保持透传，其余拇指键定义为对应 tap 值（SPC/TAB/ENT/BSPC），支持长按 repeat。NUM 层右拇指2 特例为 `KC_0`
- **Vial 自定义键码**：enum 起始用 `QK_KB_0`（非 `SAFE_RANGE`），配合 `vial.json` 的 `customKeycodes` 让 Vial GUI 正确显示名称。仅保留需要自定义逻辑的键码（SW_APP、SK_*、SW_APP_MAC），简单修饰组合用标准 `C()`/`G()`/`A()` 宏

### 已知坑

| 问题 | 根因 | 方案 |
|------|------|------|
| USB 唤醒假死（Wake-up Zombie State） | 笔记本 5V 持续供电 + RP2040 深度休眠/握手时序问题 | `config.h` 中定义 `#define NO_SUSPEND_POWER_DOWN` 和 `#define NO_USB_STARTUP_CHECK` |
| OSM 被 LT 松手吞掉 | QMK#20269，tap/hold 共用代码 | 自定义 SK_* 替代 |

| 使用中偶发整机假死（HID + raw HID 同时死，只有拔插能恢复） | `serial_vendor.c` 的 `enter_rx_state()` / `serial_transport_driver_clear()` 在 `osalSysLock()` 内有无超时忙等，PIO 状态机异常即永久死锁且中断被屏蔽（上游 QMK 同样代码） | ①`keyboards/dolphin5x/serial_vendor.c` 本地覆盖，给两处忙等加时间上界；②RP2040 硬件看门狗兜底自动复位 |
| vendored 上游文件停留在旧版本、拿不到上游修复 | 复制上游文件到键盘目录会冻结版本，上游更新不会自动跟进 | CI 用 `.github/vendored-upstream.sha256` + `sha256sum -c` 校验上游原文；一变更就构建失败并打印 diff，强制同步 |
| VPATH 覆盖可能静默失效 | 若上游改变 `serial_vendor.c` 加入 SRC 的方式，覆盖失效但编译照样成功，忙等上界悄悄丢失 | CI 构建后 grep 编译日志，确认编译的是 `keyboards/<kb>/serial_vendor.c` |
| 主手卡死后只能手动拔插 USB 才能恢复 | `SPLIT_WATCHDOG_ENABLE` 仅对从手生效（`split_watchdog_task()` 里 `!is_keyboard_master()`），主手无任何看门狗 | `dolphin5x.c` 中启用 RP2040 硬件看门狗：`watchdog_enable(4000, false)` + 在 `housekeeping_task_kb` 里 `watchdog_update()` 喂狗 |
| 启用硬件看门狗后不能去掉 `NO_USB_STARTUP_CHECK` | `protocol_pre_task()` 的 USB 挂起阻塞循环内不调用 `housekeeping_task()`，去掉该宏后休眠期间无法喂狗 | 两者互斥，保留 `NO_USB_STARTUP_CHECK` |
| 覆写 `housekeeping_task_kb` 后 `housekeeping_task_user` 被执行两次 | 该版本 `housekeeping_task()` 分别调用 `_modules`/`_kb`/`_user`，weak 的 `_kb` 是空实现（不像 `keyboard_post_init_kb` 会转调 user） | `housekeeping_task_kb` 里**不要**调用 `housekeeping_task_user()`；`keyboard_post_init_kb` 里**必须**调用 `keyboard_post_init_user()` |
| `HOLD_ON_OTHER_KEY_PRESS` 重定义 | QMK 构建系统自动注入 | config.h 不定义，靠 PERMISSIVE_HOLD |
| `get_hold_on_other_key_press` 重复定义 | `qmk_settings.c` 非 weak | 不定义此函数 |
| `combo_t key_combos` 重复定义 | `vial.c` 已定义 | 用 `eeconfig_init_user()` 写默认值 |
| `F+J` combo 跨手可能不触发 | 串口分体各半独立扫描 | 提供 Nav 层 G 位 CW_TOGG 备用 |
| `QK_BOOT` 别名不存在 | vial-qmk 版本较老 | 用 `QK_BOOTLOADER` |
| Vial GUI 自定义键码显示乱码 | `SAFE_RANGE`=`QK_USER`(0x7E40)，Vial 只识别 `QK_KB`(0x7E00) | enum 用 `QK_KB_0` 起始 + vial.json `customKeycodes` |
| 改 enum 后 Vial 仍显示旧名 | EEPROM 缓存旧键码值 | 刷固件后 File → Reset EEPROM |
| Vial UI 设置单键 LT 长按切层失效 | `update_tri_layer_state` 强制检查底层冲突 | 在 `layer_state_set_user` 中弃用原生 tri_layer 宏，完全由 `process_record_user` 内的自定义 `space_pressed` 兼容处理 || Vial 界面按键渲染错乱/拇指键不对齐 | vial.json 间距(x偏移)设置有误，或 keyboard.json 的 layout 数组未严格按物理坐标(从左到右)排序 | 检查并调整 vial.json 的偏移坐标 (如 `{"x":1}`)；keyboard.json 布局宏必须按实际界面展现的物理顺序书写 |
### 文件结构

```
keyboards/dolphin52/
├── keyboard.json          # 键盘元信息 + 52键布局定义
├── config.h               # RP2040 双击复位 + 串口 + Direct Pin 矩阵
├── dolphin52.c            # RP2040 硬件看门狗（主手卡死自恢复）
├── serial_vendor.c        # 覆盖上游 PIO 串口驱动，给无超时忙等加上界（CI 有漂移守卫）
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

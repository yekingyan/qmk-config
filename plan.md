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
- [x] 拇指切层：**固件侧不做任何层组合逻辑**，完全交给 Vial 配置。任意键可设成 `LT(n,kc)` / `MO(n)` / `TG(n)` / `TO(n)` / `LM(n,mod)`，改键界面自由指定。默认键位里拇指全是普通按键（新芯片只用基础层即可打字，不会误触切层）
  - 曾用过两套「双拇指同时按进第三层」实现，**均已于 2026-08-10 移除**：
    - 手写 `space_pressed` 等四个影子状态位：只在捕捉到松手事件时关层，漏一次事件层就永久开着（`_FUN`/`_MEDIA` 大部分位是 `XXXXXXX`，症状为「按键完全失灵」且只能断电）。而按键事件确实可能被 combo 在 `pre_process_record_quantum` 吞掉（`quantum/quantum.c:284`）
    - 官方 `update_tri_layer_state`：无影子状态不会卡死，但条件不成立时会**强制关闭**第三层（`state & ~mask3`），与「单独配一个长按键直接进 _FUN」不兼容——键刚点亮就被抹掉
  - 附带：QMK 的 Tri Layer 特性因 `VIA_ENABLE=yes` 而自动编入（`builddefs/common_features.mk:634`），若哪天真想要「两键进第三层」，可直接在 Vial 里用 `TL_LOWR`/`TL_UPPR` 键码，无需改固件
- [x] Console 日志与 Debug 调试配置（已注释关闭：`# CONSOLE_ENABLE = yes` 及 `debug_*` 注释保留，需要时随时取消注释）
- [x] 显式关闭 RGB 功能（`RGBLIGHT_ENABLE = no` 及 `RGB_MATRIX_ENABLE = no`，防止向 GP23 引脚输出灯光控制信号）
- [x] USB 唤醒防假死配置（加入 `#define NO_SUSPEND_POWER_DOWN` 及 `#define NO_USB_STARTUP_CHECK` 彻底解决笔记本 5V 持续供电导致的唤醒死机问题）
- [x] 主手卡死自恢复（`dolphin54.c` 启用 RP2040 硬件看门狗，4s 超时，在 `housekeeping_task_kb` 喂狗。QMK 的 `SPLIT_WATCHDOG_ENABLE` 只保护从手）
- [x] 分体串口死锁根因修复（`serial_vendor.c` 本地覆盖，给两处 `osalSysLock()` 内的无超时忙等加时间上界；CI 有上游漂移与覆盖失效两道守卫）
- [x] 刷机验证（2026-08-10 14:20 左右手均已刷入，枚举时刻 `14:18:32`；看门狗未误触发）
- [x] USB 失声自恢复（`dolphin5x.c`：非 `USB_ACTIVE` 且有按键活动时节流请求远程唤醒，卡满 5s 则 `mcu_reset()`。见下方「已采用的修复」）
- [ ] 刷机验证：keymap 新版 + USB 失声自恢复（**两边都要刷**）
- [ ] **失声问题待长期验证**：修复已上线但未经复现检验，需连续观察 2~3 周

### 偶发 USB 失声（旧称「卡死」）：修复已部署，待长期验证

**进度时间线**

| 日期 | 进展 |
|------|------|
| 2026-08-10 | 现象上报；确认主机侧 USB 完全正常，定位为固件主循环停摆；发现主手无看门狗 |
| 2026-08-10 | 补充信息：存活 7~8 小时到几天 → 排除计时器回绕，判定为概率性触发 |
| 2026-08-10 | 在 PIO 半双工串口驱动中定位到两处 `osalSysLock()` 内的无超时忙等（根因首选） |
| 2026-08-10 | 提交 `0b4a5ef`（根因修复 + 硬件看门狗兜底）、`5e6886d`（CI 两道守卫），推送 main |
| 2026-08-10 | CI [run 31356270778](https://github.com/yekingyan/qmk-config/actions/runs/31356270778) 全绿，11 步全 success，产物 `dolphin-vial-firmware`（含两个 uf2）已上传 |
| 2026-08-10 14:20 | **左右手均已刷入**，USB 枚举时刻 `14:18:32`，正常使用中 |
| 2026-08-10 14:24 | 已连续 6 分钟无重新枚举 → 确认**看门狗没有误触发** |
| 2026-08-10 22:15 | **再次卡死**。枚举时刻仍是 `14:18:32`（存活 ≤7h57m）→ **看门狗未触发** → 主循环是活的 → 走分支 C，PIO 串口死锁假设被否 |
| 2026-08-10 22:25 | 现场判活：Space+Tab 进 _FUN 后按左上角，**`RPI-RP2` 正常弹出** → 主循环、矩阵扫描、`process_record_user`、切层逻辑全部活着 |
| 2026-08-10 23:03 | 补充证据：卡死时**专门试过 2 次 Vial 都连不上**；**console 在正常时确认能滚 log，卡死后一行不出** → 三条独立 USB IN 端点同时死 → **keymap 假设排除，定位为设备级 USB 发送通路** |

**观察窗口**：历史 MTBF 是 7~8 小时到几天。「几天没复现」不能算修好，至少要连续正常使用 **2~3 周**。

**观察记录**（存活时长 = 两次 `LastArrivalDate` 之差）

| 枚举时刻 | 结束时刻 | 存活时长 | 结束原因 | 备注 |
|----------|----------|----------|----------|------|
| 2026-08-10 10:02:20 | ~11:33 前 | ≤91 分钟 | 卡死，手动拔插 | 修复前 |
| 2026-08-10 11:42:20 | 14:18 前 | — | 主动拔插刷机 | 修复前 |
| 2026-08-10 14:18:32 | ~22:15 | **≤7h57m** | 卡死，看门狗未触发 | 已含看门狗 + 串口上界，**均无效** |

**判读规则**：

- 枚举时刻自己变了、且你没拔线 → 看门狗触发过（曾卡死已自动恢复）→ 分支 B
- 卡死且枚举时刻不变、必须手动拔插 → 看门狗没救回来 → 分支 C（**2026-08-10 22:15 实测即此**）

**现象**（2026-08-10）：正常使用中忽然整机无响应，键盘 HID 和 Vial（raw HID）**同时**失效，只有热插拔 USB 能恢复。存活时长通常 7~8 小时，有时几天，偶尔短到 1.5 小时以内 —— 概率性触发，非周期性。

**已确认的事实**（Windows 侧 PnP 查询 + vial-qmk 源码核对）：

- 主机侧完全正常：`VID_594B/PID_D054` 三个接口 Status=OK、ProblemCode 空、电源状态 D0，卡死期间**没有重新枚举**（`LastArrivalDate` 不变），系统事件日志零条 USB/HID 错误。→ 不是线材/驱动/选择性挂起/重启循环。
- 两条 HID 通路同时死 + USB 枚举仍在（由中断维持）→ 主循环侧硬停摆。
- **主手没有任何看门狗**：`quantum/split_common/split_util.c` 的 `split_watchdog_task()` 条件是
  `if (!split_watchdog_done && !is_keyboard_master())`，`SPLIT_WATCHDOG_ENABLE` 明确只作用于从手。
  这解释了为什么必须手动拔插才能恢复。

### 根因首选：RP2040 勘误 E15（USB 设备控制器硬件锁死）

**2026-08-11 修正**：先前引用的社区 issue（#18591 休眠恢复、#19008 按键唤醒主机）
都是**休眠/唤醒**场景，与本故障「正在打字时突然失声」并不对口。而且原诊断有个薄弱处：
主机活跃时每 1ms 发一个 SOF，要凑出 `DEV_SUSPEND` 所需的 ≥3ms 空闲，在打字过程中很难发生。

真正对口的是 **RP2040-E15：USB 设备控制器会硬件锁死**。

- pico-sdk 1.5.0 起提供官方缓解 `PICO_RP2040_USB_DEVICE_UFRAME_FIX`，
  发布说明称「**This fix is required for correctness**」
- 文档记载触发条件是接 Pi 4 / Pi 400（VL805 主控），但树莓派官方论坛有专帖
  [Erratum E15 seen in field without VL805](https://forums.raspberrypi.com/viewtopic.php?t=374030)：
  *"the RP2040 USB hardware lockup described in Erratum E15 can be triggered under
  more conditions than those listed in the erratum"*
- 该勘误连数据手册都没收录（见 [pico-sdk#1260](https://github.com/raspberrypi/pico-sdk/issues/1260)）

**关键：QMK 拿不到这个缓解。** 已 grep 确认 ChibiOS 的 RP2040 USB 驱动
（`lib/chibios-contrib/os/hal/ports/RP/LLD/USBDv1/`）中 **没有任何** errata / E15 /
uframe 相关处理；pico-sdk 里那个宏只挂在 TinyUSB 构建上
（`src/rp2_common/tinyusb/CMakeLists.txt:34`），而 QMK 用的是 ChibiOS 自己的 USB 驱动。

**这一条改变了检测方式。** 硬件锁死后控制器不再产生任何中断，而
`_usb_suspend()` / `_usb_reset()` 只在中断里被调用，所以 `USB_DRIVER.state`
会**一直停在 `USB_ACTIVE`** —— 只检查状态变量完全检测不到。因此必须加硬件层面的
活性检测：`usbGetFrameNumberX()`（`hal_usb.h:419` → `USB->SOFRD & USB_SOF_RD_COUNT_Msk`，
SOF_RD 在 USB base + 0x48）。总线活跃时帧号每 1ms 递增，控制器锁死则冻结。

**「bootloader 能进」不能区分两类根因。** `reset_usb_boot()` 进 bootrom，
bootrom 会复位 USB 外设块并用自己的栈重新枚举，所以硬件锁死照样能弹出 `RPI-RP2`。
它能证明的仍然是：MCU、主循环、矩阵、keymap 都活着，故障在 USB 通路。

**好消息：现有的第 2 级手段对 E15 同样有效。** `restart_usb_driver()` 会先
`usbStop()` 把状态置为 `USB_STOP`，随后 `usbStart()` → `usb_lld_start()` 在
`state == USB_STOP` 分支里执行：

```c
hal_lld_peripheral_reset(RESETS_ALLREG_USBCTRL);    // 整个 USB 外设块硬件复位
hal_lld_peripheral_unreset(RESETS_ALLREG_USBCTRL);
memset(USB, 0, sizeof(*USB));                       // 寄存器清零
memset(USB_DPSRAM, 0, sizeof(*USB_DPSRAM));         // DPRAM 清零
```

与进 bootrom 时 bootrom 做的是同一类复位，所以能清除硬件锁死，且**不重启 MCU**。

### 次要候选：设备级 USB 驱动状态卡在非 `USB_ACTIVE`

仍保留为候选（检测条件 (a) 覆盖它）。机制与证据：三条相互独立的 USB IN 端点同时死掉，
而主循环侧证明完全正常：

| 通路 | 接口 | 卡死时表现 | 是否经过 keymap / 层状态 |
|------|------|-----------|------------------------|
| 键盘 HID | MI_00 | 按键无反应 | 经过 |
| raw HID (Vial) | MI_01 | 连不上（专门试过 2 次）| **完全不经过** |
| Console | 额外接口 | 无输出（正常时已确认会滚 log）| **完全不经过** |

三者共用同一个门（`tmk_core/protocol/chibios/usb_driver.c` 的 `usb_endpoint_in_send()`）：

```c
if (usbGetDriverStateI(endpoint->config.usbp) != USB_ACTIVE) {
    return false;   // 静默丢弃。usbp 是设备级对象，一个变量卡住三个接口一起死
}
```

**为什么没有任何自愈**（均已核对源码）：

- `USB_DRIVER.state == USB_SUSPENDED` 的检测与 `usbWakeupHost()` 全代码库**各只有一处**，
  都在 `tmk_core/protocol/chibios/chibios.c` 第 180~201 行的
  `#if !defined(NO_USB_STARTUP_CHECK)` 块内 → 我们定义了这个宏，被编译掉。
- LLD 里 SOF 处理有一条 `if (state == USB_SUSPENDED) _usb_wakeup(usbp);`，但基线
  `USB->INTE` 未开 `DEV_SOF`（固件二进制中实测该值为 `0x0001d010`，bit17 确实为 0）；
  按 `usb_lld_wakeup_host` 宏的注释，SOF 中断只在发起远程唤醒时才打开。
- `DEV_RESUME_FROM_HOST` 需要主机真的发 resume，但主机侧显示 D0/无错误，不认为设备挂起过。
- `BUS_RESET` 只有重新枚举（拔插）才有。

所以根源是 `NO_USB_STARTUP_CHECK`：它去掉了挂起时的阻塞（我们要的），
同时也去掉了唤醒时的恢复（我们不要的）。

**尚未确定：卡在哪个非 ACTIVE 状态**，这决定修法：

| 状态 | 触发条件 | 上游是否有恢复路径 |
|------|---------|------------------|
| `USB_SUSPENDED` (5) | 总线空闲 ≥3ms | 有（`usbWakeupHost()`，被宏编译掉了）|
| `USB_READY` (2) | 误检 BUS_RESET，`_usb_reset()` 打回 READY 等主机重新配置，而主机不知道 | **完全没有** |

用 console 抓这个状态变化**行不通**：`dprintf` 自身也走 `usb_endpoint_in_send()`，
状态一变日志同时被丢弃。可行的判别是看恢复方式——恢复且枚举时刻不变 = SUSPENDED
且远程唤醒生效；恢复且枚举时刻变了 = 走了复位兜底，即不是 SUSPENDED。

**已排除的假设**：

- **keymap.c / 切层**（2026-08-10 23:03 排除）：Vial 与 console 都不经过 keymap 和层状态，
  它们同时死无法用「层卡住」解释。另外 QMK 有来源层缓存（`quantum/action_layer.c` 的
  `store_or_get_action`：按下 `update_source_layers_cache`，松开 `read_source_layers_cache`），
  「按住时切层导致松手键码变了、`*_pressed` 漏清」这条具体路径也是堵住的。
  - **教训**：中途我曾用「bootloader 能进 ⇒ 层没卡住」来排除 keymap，这个论证是**错的**。
    它只能排除比 _FUN 更高的层（_MEDIA=6、_NAV_MAC=7）。_FUN 自身卡住时，左手拇指是
    `_______`（透传成 Space/Tab）、左上角正是 `QK_BOOTLOADER`，所有观察都吻合。
    真正定案靠的是 Vial + console 这两条不经过 keymap 的通路。
- **PIO 半双工串口死锁**（2026-08-10 22:15 排除）：那类故障会让主循环停摆、看门狗必然触发。
  实测看门狗未触发。该隐患真实存在但不是本故障的原因。
- **ChibiOS 32 位 @1MHz 计时器回绕（71.58 分钟）**：回绕是确定性的，与「7~8 小时 / 几天」矛盾。
- **开机主从判定竞态**：那种情况从插上就不工作。
- **主机休眠/唤醒**：查过 Windows 事件日志，14:00~22:15 期间无 Kernel-Power 42/107。
- **修饰键卡住**：`GetAsyncKeyState` 实测全部为 up。

**已做但被证明对本故障无效的处置**（保留，各自兜别的风险）：

- RP2040 硬件看门狗：只能救「主循环停摆」，本故障主循环是活的，狗照喂 → 无效。
- `serial_vendor.c` 忙等上界：修的是真实上游隐患，但已证明不是本故障原因。
  **这块是过度设计**——代价是 vendored 一个上游时序敏感驱动 + 两道 CI 守卫 + 版本同步负担，
  而它所修的隐患恰好能被硬件看门狗兜住。是否撤掉待定。


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

### 已采用的修复：方案 2（状态无关的 USB 失声自恢复）

**先纠正一个用词**：这不是「卡死」，是「**失声**」。MCU、主循环、矩阵扫描、keymap、切层
全都正常运行，坏的只是 USB 发送通路——报文照样生成，然后被静默丢弃。

这就是为什么 `_FUN` 层的 `QK_BOOTLOADER` 按得出来：`bootloader_jump()` 在 RP2040 上是
`reset_usb_boot()`（`platforms/chibios/bootloaders/rp2040.c:19`），而它是
`rom_func_lookup(ROM_FUNC_RESET_USB_BOOT)` 查表后**跳进 bootrom**
（`lib/pico-sdk/.../pico/bootrom.h:161`）。bootrom 用自己的 USB 栈从零初始化控制器，
完全绕过 ChibiOS 驱动和 `usb_endpoint_in_send()` 那道门。

反过来说：**正因为固件逻辑是活的，软件层自救才可行**。如果真是整机停摆，就只有硬件看门狗
能救（而它已实测无效——狗一直在被喂）。

**检测条件**（两个取或，覆盖两类根因）：

| 条件 | 覆盖的根因 |
|------|-----------|
| (a) `USB_DRIVER.state != USB_ACTIVE` | 软件状态机卡住（误检 DEV_SUSPEND 卡 `USB_SUSPENDED`，或误检 BUS_RESET 被打回 `USB_READY` 等主机重新配置而主机不知情）|
| (b) `usbGetFrameNumberX()` 帧号停止推进 > 100ms | **E15 硬件锁死**。此时 state 仍为 `USB_ACTIVE`，只看 (a) 检测不到 |

条件 (b) 只在 `state == USB_ACTIVE` 时才读帧号。原因：`USB->SOFRD` 是读清型寄存器
（LLD 的 ISR 里用 `(void)USB->SOFRD;` 清 SOF 中断标志），而 `usbWakeupHost()` 会打开
`DEV_SOF` 中断，若无条件轮询会与 LLD 中基于 SOF 的自愈路径抢标志。常态下
`USB->INTE` 未开 `DEV_SOF`（实测 `0x0001d010`），所以这样限定后轮询无害。

**恢复动作**（三级递进，代价由轻到重，共用同一套按键活动门控）：

| 级别 | 触发时机 | 动作 | 对 E15 有效？ | 代价 |
|------|---------|------|--------------|------|
| 1 | 每 250ms | `usbWakeupHost()` | ✗（state 仍 ACTIVE，内部判断不通过）| 无 |
| 2 | 卡满 2s，每 3s 重试 | `restart_usb_driver()` | **✓ 外设块硬件复位** | 主机重新枚举，MCU 不重启 |
| 3 | 卡满 10s 且曾 ACTIVE 过 | `mcu_reset()` | ✓ | 等效自动拔插一次 |

三重门控（全部三级共用）：

| 门控 | 防什么 |
|------|--------|
| 3 秒内有按键活动，且本次上电按过键 | 主机真休眠、用户没在用时保持安静，不反复唤醒主机。显式排除 `last_key_activity == 0`，否则 `timer_elapsed32(0)` 在开机头几秒会被误判成「刚有按键活动」。语义对齐上游 `suspend_wakeup_condition()` |
| `was_active`（本次上电曾 ACTIVE 过）| 接充电头（有 5V 无主机）时按键不会导致反复复位。**仅第 3 级需要** |
| 时间门限 | 短暂抖动不触发 |

### 社区调研：这是已知的上游问题，且没有修复

| 来源 | 内容 | 对我们的意义 |
|------|------|-------------|
| [树莓派论坛 t=374030](https://forums.raspberrypi.com/viewtopic.php?t=374030) | **E15 硬件锁死在现场被观察到，且触发条件比勘误记载的更宽**（不限于 VL805 主机）| **根因首选**。硬件锁死 + 只有复位能救 + 随机触发，与本故障吻合 |
| [pico-sdk#1260](https://github.com/raspberrypi/pico-sdk/issues/1260) | E15 的官方缓解 `PICO_RP2040_USB_DEVICE_UFRAME_FIX`，「required for correctness」；且该勘误未收录进数据手册 | 缓解只挂在 TinyUSB 构建上；ChibiOS 的 RP2040 USB 驱动**零** errata 处理（已 grep 确认）→ QMK 拿不到 |
| [qmk#7784](https://github.com/qmk/qmk_firmware/pull/7784) | `usb_endpoint_in_send()` 里 `!= USB_ACTIVE` 就丢弃的来源。原文：*"If the status is not USB_ACTIVE, we don't have any endpoints and attempting to send on them crashes. Discard these sends."* | 静默丢弃是**有意为之**（避免崩溃），不是 bug。所以永远不会有上游补丁让非 ACTIVE 状态下也能发送 —— 唯一出路是回到 ACTIVE |
| [qmk#14851](https://github.com/qmk/qmk_firmware/issues/14851) | *"This behaviour blocks the keyboard_task() from being invoked, which would then start the matrix_scan()"* | 印证 `NO_USB_STARTUP_CHECK` 的阻塞循环会挡住主循环，即方案 1 的悖论 |
| [qmk#18591](https://github.com/qmk/qmk_firmware/issues/18591) | kb2040(RP2040) 从 Windows 休眠恢复后无响应，*"If I reset the MCU, the keyboard will be recognized"*，至今 OPEN | **场景不同**（休眠恢复，非使用中失声），仅作旁证：同芯片同类问题上游未解，复位是已知解法 |
| [qmk#19008](https://github.com/qmk/qmk_firmware/issues/19008) | 部分 RP2040 按键无法唤醒主机 | **场景不同**（休眠唤醒）。价值在于从它贴的上游代码里发现了 `restart_usb_driver()` |

结论：这不是我们的配置搞坏了什么，而是 RP2040 + ChibiOS USB 栈的一个上游未解问题。
既然上游没有修复、且社区公认的解法就是复位，那么在自己键盘目录里做自动恢复是合理的选择。

**为什么没选方案 1（删 `NO_USB_STARTUP_CHECK` + `suspend_power_down_kb()` 喂狗）**：

1. 方案 2 已经包含了方案 1 的核心机制。方案 1 起作用的链是
   `usbWakeupHost()` → `usb_lld_wakeup_host` 宏 → `USB->INTE |= DEV_SOF` → 主机本来就在
   每 1ms 发 SOF（它压根不认为设备挂起过）→ SOF 中断 → LLD 里
   `if (state == USB_SUSPENDED) _usb_wakeup()` → 恢复。
   **这条链只要有人调 `usbWakeupHost()` 就通，不需要删那个宏。**
2. 方案 1 有个悖论：删掉宏后阻塞循环回来，为了不让主机休眠时误复位**必须**在循环里喂狗，
   但这样看门狗就永远救不出一个卡住的 suspend 循环——而那恰恰就是本故障。
3. 方案 1 只覆盖 `USB_SUSPENDED`；若卡在 `USB_READY`，那个循环压根不会进入。
4. 方案 1 要改目前**正常工作**的休眠行为，有让当初「唤醒假死」回归的风险。

### 下次失声时怎么做（操作手册）

#### 情况 A：它自己恢复了（预期结果）

**唯一必须做的事：告诉我，我去读 `LastArrivalDate`。** 这一个数据点就能定案：

| 观察 | 结论 |
|------|------|
| 恢复了，枚举时刻**没变** | 第 1 级生效：是 `USB_SUSPENDED`，远程唤醒 + SOF 自愈救回来了 |
| 恢复了，枚举时刻**变了**，约 2 秒 | 第 2 级生效：`restart_usb_driver()` 的外设块复位救回来了（MCU 没重启）。**若根因是 E15 硬件锁死，预期就是这一档** |
| 恢复了，枚举时刻**变了**，约 10 秒 | 第 3 级生效：连 USB 外设块复位都无效，只有整芯片复位能救 |

三种情况对应的根因不同，所以**恢复耗时和枚举时刻这两个数据必须一起记**。
把结果追加到上面的观察记录表。

#### 情况 B：仍然需要手动拔插（修复无效）

**拔线之前**按顺序做，第 1 步最关键：

1. **先按几个键，等 6 秒。** 恢复逻辑需要「按键活动」才会动作——一发现失声就干瞪眼的话
   它压根不会尝试。
2. 还是死的 → 报给我，我读枚举时刻 + 电源状态（D0/D2/D3）+ 系统事件日志
3. 试连 Vial（raw HID 是否也死）
4. Space+Tab 进 `_FUN` 再按左上角，看 `RPI-RP2` 是否弹出（确认主循环还活着）
5. 然后才拔插

若第 1 步按键后仍不恢复，下一步是加**看门狗 SCRATCH 寄存器诊断**：`watchdog_hw->scratch[0..3]`
未被占用（pico-sdk 的 `watchdog_enable()` 只写 `scratch[4]`，`watchdog_reboot()` 用
`scratch[4..7]`，bootrom 也只看这几个），这些寄存器跨复位保留，可以在恢复后把上次卡住的
`USB_DRIVER.state` 值打出来。

#### 情况 C：出现新毛病

| 症状 | 含义 | 应对 |
|------|------|------|
| 枚举时刻频繁变化、时不时断一下 | 复位兜底误触发 | `DOLPHIN_USB_STUCK_RESET_MS` 从 5000 调大 |
| 主机休眠唤醒后异常 | 意外——本方案未改动任何 suspend 配置 | 立刻报 |
| 打字莫名丢字 | 不该发生，恢复逻辑不碰正常路径 | 报 |

#### 顺带：keymap 新版刷完要验证

- 内侧四个拇指长按能否进 _NAV / _NUM / _SYM / _MOUSE
- 外侧两个拇指配好后能否进 _FUN / _MEDIA（**刷前先在 Vial 里配好**，否则这两层暂时进不去）
- Caps Word 下按 `-` 出 `_`、按数字出数字
- J+K 仍能出 Shift（确认 EEPROM 里的 combo 没丢）

**结案标准**：连续正常使用 2~3 周无失声 → 勾掉状态项，把本节压缩成「已知坑」表里一行。


**分支 D — 出现新问题：右半区失灵 / 按键丢失 / 分体不同步**

优先怀疑是串口上界改动的副作用（3819µs 超时在正常情况下被误触发，导致事务被丢弃）。

- 快速验证方式是**把上界调大**，而不是删文件：把 `SERIAL_PIO_DRAIN_TIMEOUT_US` 的倍数
  从 10 改成 1000（≈382ms），等效于关掉上界但保留文件结构，两道守卫都不受影响。
  若问题消失即证实是误触发。
- 注意：**不要用「删掉 `keyboards/dolphin5x/serial_vendor.c`」来做对比测试**。
  删了之后守卫 2 会失败，而它在构建步骤之后、artifact 上传之前 —— job 会中断，
  **拿不到任何固件产物**。真要回落到上游版本，必须同时把工作流里
  `Verify keyboard-level serial_vendor.c override took effect` 这一步一起去掉。
- 若确认是误触发：当前上界只需大于「8 字节 × 11bit / 波特率」，230400bps 下理论最坏 382µs，
  10 倍已很宽松。真误触发说明有别的因素在拖慢 FIFO 排空，值得先查清原因再调参。

**分支 E — CI 构建失败**

看是哪道守卫失败：

- `Guard vendored upstream files against drift` → 上游改动了 `serial_vendor.c`。
  按 `.github/vendored-upstream.sha256` 注释里的 4 步同步：拿上游新版覆盖 → 重新施加两处上界
  → cp 到另一个键盘目录 → 更新基线哈希。日志里会打印新旧 diff，直接照着改。
- `Verify keyboard-level serial_vendor.c override took effect` → VPATH 覆盖机制变了。
  重新确认 `builddefs/common_features.mk` 里 `QUANTUM_LIB_SRC += serial_$(SERIAL_DRIVER).c`
  是否仍按裸文件名加入，以及 `build_keyboard.mk` 的 VPATH 顺序是否仍是
  `KEYMAP_PATH` → `USER_PATH` → `KEYBOARD_PATHS` → `COMMON_VPATH`。

### 诊断工具箱（本次排查中验证有效的手段，复用备查）

**从 WSL 查 Windows 侧 USB 真实状态**（WSL2 无 USB 子系统，`/sys/bus/usb`、`/dev/hidraw*` 都不存在，
必须绕道 PowerShell）：

```powershell
# 设备是否在、有无问题码
Get-PnpDevice -PresentOnly | Where-Object {$_.InstanceId -like '*VID_594B*'} |
  Select-Object Status,ProblemCode,FriendlyName,InstanceId

# 精确的枚举时刻 —— 判断卡死期间有没有重新枚举、以及算存活时长
(Get-PnpDeviceProperty -InstanceId '<id>' -KeyName 'DEVPKEY_Device_LastArrivalDate').Data

# 是否被选择性挂起：解码 PowerData 的 MostRecentPowerState（D0 = 正常供电）
$p = (Get-PnpDeviceProperty -InstanceId '<id>' -KeyName 'DEVPKEY_Device_PowerData').Data
'D' + ([BitConverter]::ToInt32($p,4) - 1)
```

**查是否有修饰键被固件卡住**（能一次排除「RGUI/LALT 常按导致看起来没反应」）：
`user32.dll` 的 `GetAsyncKeyState`，检查 `0x5B/0x5C/0xA0-0xA5` 的 `0x8000` 位。

**判断卡死类型的核心逻辑**：键盘 HID 与 raw HID（Vial）**是否同时失效**。
两者都在主循环里处理，而 USB 枚举由中断维持 —— 所以「主机侧显示设备正常但两条 HID 都不通」
就等于主循环停摆，而不是 USB/驱动/线材问题。

**本地编译验证**（仓库无 submodule，CI 才 clone vial-qmk；本地要自己拉）：

```bash
git clone --depth 1 -b vial https://github.com/vial-kb/vial-qmk.git /tmp/vial-qmk
cd /tmp/vial-qmk && git submodule update --init --depth 1 \
    lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf
cp -r ~/projects/qmk-config/keyboards/dolphin54 keyboards/ && make dolphin54:vial
```

**LTO 会吃掉符号，验证代码是否真的生成要看反汇编**：本次 `watchdog_enable`/`watchdog_update`
在 `nm` 里完全查不到（被内联），只能靠 `objdump -d` 找字面量池核对，例如
`0x6ab73121`(watchdog magic)、`0x007a1200`(4000ms 的 load 值)、`0x00000eeb`(3819µs 上界)。
写完固件改动后如果只看「编译通过」，可能什么都没生效。

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
- **Combo**：S+D=Esc、J+K=LShift、F+J=CapsWord（F+J 跨手可能不触发，备用 Nav 层 G 位 CW_TOGG）
  - **必须在 `keyboard_post_init_user()` 里播种进 EEPROM，不能用官方静态定义**：
    vial-qmk 在 `COMBO_ENABLE=yes` 时无条件定义 `VIAL_COMBO_ENABLE`（`quantum/vial.h:108`）
    并自己定义 `combo_t key_combos[]`（`quantum/vial.c:556`），官方那种在 keymap.c 里写
    `combo_t key_combos[] = { COMBO(...) }` 会重复定义、链接失败。
  - 播种覆盖「全新芯片首刷」场景：`wear_leveling_init()` 开头就 `memset(cache, 0, LOGICAL_SIZE)`，
    且全新芯片上 FNV1a_64 校验必然失败而再次清零（`quantum/wear_leveling/wear_leveling.c:618-647`），
    所以 EEPROM 全读 0 → `entry.output == 0` 成立 → 播种触发。
  - 播种后那段「刷新 `key_combos[]` 内存数组」不是冗余代码：`vial_init()`（内含 `reload_combo()`）
    在 `keyboard_setup()` 里执行（`quantum/keyboard.c:370`），**早于** `keyboard_post_init_user()`，
    不手工刷一次的话新芯片第一次上电要等重启组合键才生效。
  - `memcpy` 拷贝长度必须用 `sizeof(keys[i])`(6 字节) 而非 `sizeof(entry.input)`(8 字节)，
    后者会越界读源数组 2 字节（2026-08-10 修）。
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

| 使用中偶发整机假死（键盘 HID + raw HID + console 三条 IN 端点同时死，只有拔插能恢复） | 设备级 `USB_DRIVER.state` 卡在非 `USB_ACTIVE`，`usb_endpoint_in_send()` 入口静默丢弃全部报文；唯一的挂起检测与 `usbWakeupHost()` 都在 `#if !defined(NO_USB_STARTUP_CHECK)` 块内被编译掉 | 待定：方案 1（删该宏 + `suspend_power_down_kb()` 喂狗）或方案 2（非 ACTIVE 且有按键活动超时 → `mcu_reset()`）。硬件看门狗对此**无效**（主循环活着，狗照喂）|
| 排查时误用「bootloader 能进」排除 keymap 层卡住 | 该论证只能排除比 _FUN 更高的层；_FUN 自身卡住时左手拇指透传成 Space/Tab、左上角正是 `QK_BOOTLOADER`，观察完全吻合 | 用不经过 keymap 的通路判别：Vial(raw HID) 与 console 是否也死 |
| `serial_vendor.c` 忙等上界（保留但属过度设计） | 修的是真实上游隐患，但已证明不是本故障原因 | 代价是 vendored 上游驱动 + 两道 CI 守卫 + 同步负担，而该隐患恰好能被硬件看门狗兜住；是否撤掉待定 |
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

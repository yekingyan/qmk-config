# QMK 项目驾驶舱

> ## 🔧 接手指引（零上下文时先读这里）
>
> **本仓库当前有一个未结案问题：Dolphin54 偶发「USB 失声」。**
> 用着用着（通常 7~8 小时到几天）键盘突然完全没反应，主机侧却显示设备正常。
>
> **✅ 2026-08-11 13:23 已刷入完整修复（PR #92 / `843a677`），观察窗口从此重新计起。**
> 这一版含：自伤修复、门控拆两档、诊断计数器 + `USB_DIAG` 键、
> 第 3 级改 `watchdog_reboot()`、Sticky master。同时**键盘换到了 `Port_#0003`**
> （原 `Port_#0006`）—— 物理层变了，是个需要一起考虑的变量。
>
> 之前 `0acbfeb` 版已证明自动恢复**确实有效**（2026-08-11 10:18 起多次卡 2 秒自恢复），
> 但那版有「恢复逻辑打断自己枚举」的缺陷，一上午留下 4 次失败枚举 + 主机弹「USB 不识别」。
>
> ### 用户报「键盘卡死 / 没反应 / 卡了一下」时，第一步：
>
> ```bash
> scripts/kb-diag.sh          # 当前状态 + 与基线对比 + 自动记录事件
> scripts/kb-diag.sh --history # 看完整事件历史（含「枚举失败」与「刷机」记录）
> scripts/kb-diag.sh --log    # 若仍是死的，加这个看错误与休眠事件
> ```
>
> 观察期建议常驻盯着，否则事件会漏（Windows 不记录已安装设备的重新枚举）：
>
> ```bash
> nohup setsid scripts/kb-diag.sh --watch 60 >> scripts/.kb-watch.out 2>&1 < /dev/null &
> ```
>
> **它是 WSL 内的后台进程，WSL 关闭 / 电脑重启后会没** —— 重启后要重新挂，
> 并且重启会改变枚举时刻，记得同时跑一次 `--baseline`。
> 检查是否还在跑：`pgrep -af 'kb-diag.sh --watch'`
>
> 另外键盘上（`_FUN` 层左上 `QK_BOOTLOADER` 正下方）的 `USB_DIAG` 键能把固件侧的计数敲出来，
> 直接告出是「软件状态卡住」还是「E15 硬件锁死」。
>
> **刷机后用 `--flashed`（不是 `--baseline`）**，它会打一个刷机标记，
> 让「枚举失败」只在晚于刷机时刻时才告警 —— 换 USB 口时尤其重要。
>
> 然后问用户两个问题：**卡了大约几秒？期间有没有手动拔插或重启电脑？**
> 有了这两个答案 + 脚本输出即可判读：
>
> | 脚本显示 | 用户体感 | 结论 |
> |---------|---------|------|
> | 枚举时刻**未变** | 卡一下就好了 | 第 1 级 `usbWakeupHost()` 生效（`USB_SUSPENDED`）|
> | 枚举时刻**变了** | 约 2 秒 | 第 2 级 `restart_usb_driver()` 生效 ← **RP2040 勘误 E15 的预期表现** |
> | 枚举时刻**变了** | 约 10 秒 | 第 3 级整芯片复位（`watchdog_reboot`）生效，连驱动重启都救不回来 |
> | 枚举时刻未变 | 一直是死的 | 修复未生效 → 走「情况 B」，**拔线前先按几个键等 6 秒** |
> | 枚举时刻反复变 | 时不时断一下 | 复位误触发 → 调大 `DOLPHIN_USB_STUCK_RESET_MS` |
> | 枚举时刻反复变 | **弹「USB 不识别」** | 恢复逻辑打断了自己的枚举 → 调大 `DOLPHIN_USB_RECOVERY_GRACE_MS`（2026-08-11 已修一次）|
>
> ### 拿到结论后要做的事
>
> 1. 把这次事件追加到「观察记录表」（本文档内，搜 `观察记录`）
> 2. 让用户跑 `scripts/kb-diag.sh --baseline` 更新基线
> 3. 若连续 2~3 周无复发 → 可以结案，勾掉状态项
>
> ### 重要前提
>
> - **刷机、重启电脑都会改变枚举时刻**，这两件事之后必须重新 `--baseline`，否则误报
> - **刷机会把 Vial 键位配置清回默认**（`BUILD_ID` 每次构建随机 → VIA magic 必然失配）。
>   而**默认键位刻意不含任何切层键 —— 这是已定案的设计，不要提议改**。
>   所以刷完先去 Vial 配切层键是正常流程；配好前 `_FUN` 进不去，
>   刷完先去 Vial 配切层键是正常流程。详见「状态」里「拇指切层」条目下的定案说明
> - **进刷机模式只走「Vial 改键 → 按键」这一条路**。板载 reset 按钮在壳里，
>   要拆机才能按，所以双击物理 reset 在本键盘上**不是可用手段**。详见「怎么进刷机模式」一节
> - 这个故障**不是** keymap / 切层问题（已用 Vial + console 两条不经过 keymap 的通路排除）
> - 硬件看门狗对它**无效**（主循环是活的，狗一直在被喂）
>
> 详细分析见下方「偶发 USB 失声」整节；操作细节见「下次失声时怎么做（操作手册）」。

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

  > ### ✅ 已定案，不要再提议改（2026-08-11 用户明确确认）
  >
  > **默认键位就该是「不切层」。切层是进阶用法。刷完后在 Vial 里配切层键是正常流程，不是缺陷。**
  >
  > 所以不要再提出「在默认键位里加 `MO(_FUN)` 之类让刷完立刻能切层」的建议。
  > 由此带来的两个已知后果都是**预期行为**，不需要「修」：
  > - 刷完后 `_FUN` / `_MEDIA` 进不去，直到在 Vial 里配好切层键
  > - 因此 `QK_BOOTLOADER` 与 `USB_DIAG` 也按不出来，直到在 Vial 里配好切层键
  >   （**注意：本键盘不能靠双击物理 reset 兜底，reset 按钮在壳里要拆机**，
  >   见「怎么进刷机模式」一节）
  - 曾用过两套「双拇指同时按进第三层」实现，**均已于 2026-08-10 移除**：
    - 手写 `space_pressed` 等四个影子状态位：只在捕捉到松手事件时关层，漏一次事件层就永久开着（`_FUN`/`_MEDIA` 大部分位是 `XXXXXXX`，症状为「按键完全失灵」且只能断电）。而按键事件确实可能被 combo 在 `pre_process_record_quantum` 吞掉（`quantum/quantum.c:284`）
    - 官方 `update_tri_layer_state`：无影子状态不会卡死，但条件不成立时会**强制关闭**第三层（`state & ~mask3`），与「单独配一个长按键直接进 _FUN」不兼容——键刚点亮就被抹掉
  - 附带：QMK 的 Tri Layer 特性因 `VIA_ENABLE=yes` 而自动编入（`builddefs/common_features.mk:634`），若哪天真想要「两键进第三层」，可直接在 Vial 里用 `TL_LOWR`/`TL_UPPR` 键码，无需改固件
- [x] Console 日志与 Debug 调试配置（已注释关闭：`# CONSOLE_ENABLE = yes` 及 `debug_*` 注释保留，需要时随时取消注释）
- [x] 显式关闭 RGB 功能（`RGBLIGHT_ENABLE = no` 及 `RGB_MATRIX_ENABLE = no`，防止向 GP23 引脚输出灯光控制信号）
- [x] USB 唤醒防假死配置（`#define NO_SUSPEND_POWER_DOWN` + `#define NO_USB_STARTUP_CHECK`，解决笔记本 5V 持续供电导致的唤醒死机）
  - ⚠️ **`NO_USB_STARTUP_CHECK` 同时也是「USB 失声」的根源之一**：它把上游唯一的挂起检测与
    `usbWakeupHost()` 调用一起编译掉了（`chibios.c` 第 180~201 行）。但**不能简单删掉它**，
    理由见下方「为什么没选方案 1」。
- [x] 主手卡死自恢复（`dolphin5x.c` 启用 RP2040 硬件看门狗，4s 超时，在 `housekeeping_task_kb` 喂狗。QMK 的 `SPLIT_WATCHDOG_ENABLE` 只保护从手）
  - ⚠️ **对「USB 失声」无效**（已实测：主循环没停、狗一直在被喂、8 小时未触发）。它兜的是另一类风险：主循环真正停摆
- [x] 分体串口死锁根因修复 —— **2026-08-11 已撤销**（`serial_vendor.c` 本地覆盖 + 两道 CI 守卫全部移除）
  - 撤销理由：它修的上游隐患真实存在，但已证明**不是**「USB 失声」的原因，而那个隐患恰好能被
    `dolphin5x.c` 里的硬件看门狗兜住（串口死锁会让主循环停摆 → 狗必然触发）。
    代价却是 vendored 一个上游时序敏感驱动 + 两道 CI 守卫 + 长期版本同步负担。
  - 反汇编确认已回落到上游版本：忙等上界常量 `0x00000eeb`(3819µs) 已从两个二进制中消失
- [x] 刷机验证（2026-08-10 14:20 左右手均已刷入，枚举时刻 `14:18:32`；看门狗未误触发）
- [x] USB 失声三级自恢复（`dolphin5x.c`：检测「状态非 `USB_ACTIVE`」或「硬件帧计数器停滞 >100ms」，配合按键活动门控，依次尝试 `usbWakeupHost()` → `restart_usb_driver()`(2s) → `watchdog_reboot()`(10s)。详见下方「已采用的修复」）
- [x] **刷机：完整修复已刷入双手**（2026-08-11 13:23，PR #92 / `843a677`）

  | # | 改动 | 说明 |
  |---|------|------|
  | P0 | **恢复逻辑自伤修复** | 第 2 级做完进 5s 宽限期整段静默，且每个失声周期只做一次。修的是「Windows 弹 USB 不识别」，详见「自伤」一节 |
  | #5 | **按键活动门控拆两档** | 第 1 级（真会敲主机的那一下）保持 3s；第 2/3 级放宽到 10s（`DOLPHIN_USB_RECOVERY_WINDOW_MS`）。原来一停手就不自救 |
  | #4 | **固件诊断计数器 + `USB_DIAG` 键码** | 条件(a)/(b) 分别计数、自愈次数、restart 次数、跨复位的芯片复位次数、卡住时的 state。按键用 `send_string` 打出来。字段含义见 `users/vial/dolphin5x.h`。**键位在 `_FUN` 层左上 `QK_BOOTLOADER` 正下方**（矩阵 `[1,0]`，原来是空位） |
  | #6 | **消除 dolphin52.c/54.c 重复** | 合并为 `users/vial/dolphin5x.c`，走 QMK userspace 机制。CI 新增守卫确认它真的被编进去了 |
  | #9 | **撤掉 `serial_vendor.c` 本地覆盖** | 连同两道旧 CI 守卫一起移除 |
  | #2 | **第 3 级改用 `watchdog_reboot()`** | `mcu_reset()` = `NVIC_SystemReset()` 不复位外设；`watchdog_reboot` 经 `psm_hw->wdsel` 复位「除 ROSC/XOSC 之外的一切」，**包含 USB 外设块**。顺带提供可靠的软复位判据 `watchdog_hw->reason` |
  | #2 | **Sticky master** | 覆盖 `is_keyboard_master_impl()`，软复位后用 `scratch[1]` 沿用主手身份 → 消除「复位后枚举超时 → 判成从手 → `usb_disconnect()` → 设备从主机消失」这条恶性路径。CI 有上游语义守卫 |
  | #3 | **`scripts/kb-diag.sh` 增强** | 失败枚举检测 + append-only 事件历史 + `--watch` 定时采样 + `--flashed` 刷机标记 |

- [ ] **失声问题长期验证中**：观察起点 **2026-08-11 13:23:38**（端口也从 `Port_#0006` 换到了 `Port_#0003`），需累计 2~3 周**实际使用**无「不可恢复失声」

  刷完待做（用户自行操作）：
  1. ~~Vial 里配切层键~~ ✅ 已配
  2. ~~验证 `USB_DIAG`~~ ✅ 2026-08-11 14:16 读数 `a=0 b=0 heal=0 rst=0 mcu=0 st=255 up=3146s`，
     且 `up` 与主机侧枚举时刻交叉验证吻合
  3. ~~挂上被动监测~~ ✅ 2026-08-11 14:17 起 `kb-diag.sh --watch 60` 常驻

  > ⚠️ **watcher 是 WSL 内的后台进程，WSL 关闭 / 电脑重启后会没**。
  > 重启后要重新挂一次，并且**重启会改变枚举时刻**，所以还要跑一次 `--baseline`
  > （注意是 `--baseline` 而不是 `--flashed` —— 没换固件就不要动刷机标记）：
  > ```bash
  > cd ~/projects/qmk-config
  > scripts/kb-diag.sh --baseline
  > nohup setsid scripts/kb-diag.sh --watch 60 >> scripts/.kb-watch.out 2>&1 < /dev/null &
  > ```

  待定未做：#8（wear leveling 扩容，与失声无关，用户已决定先不动）
- [x] 诊断脚本 `scripts/kb-diag.sh`（一条命令抓齐 USB 状态、运行时长、卡住的修饰键、与基线对比）
- [x] **刷机：keymap 新版 + USB 失声三级自恢复**（2026-08-11 刷入 `0acbfeb` / PR #91 产物，左右手均已刷；随后重启电脑，当前枚举时刻 `01:00:25`，基线已于 01:01 记录）
- [ ] **失声问题待长期验证**：修复已刷入设备（`0acbfeb`）。**2026-08-11 10:18:39 已首次证明有效**（第 2 级 `restart_usb_driver` 自动救回，用户仅感到卡 2 秒 ×2、无需拔插）。观察窗口自 `10:18:39` 重新计，需累计 2~3 周**实际使用**无「不可恢复失声」
- [ ] 刷机后功能验证（Vial 配外侧拇指进 _FUN/_MEDIA、内侧四拇指长按切层、Caps Word、J+K combo）—— 见文末「顺带：keymap 新版刷完要验证」

### 偶发 USB 失声（旧称「卡死」）：修复**已刷入设备**，长期验证中（观察起点 2026-08-11 01:00:25）

**进度时间线**

| 日期 | 进展 |
|------|------|
| 2026-08-10 | 现象上报；确认主机侧 USB 完全正常。**当时误判为「固件主循环停摆」（后被推翻）**；发现主手无看门狗 |
| 2026-08-10 | 补充信息：存活 7~8 小时到几天 → 排除计时器回绕，判定为概率性触发 |
| 2026-08-10 | 在 PIO 半双工串口驱动中定位到两处 `osalSysLock()` 内的无超时忙等（根因首选） |
| 2026-08-10 | 提交 `0b4a5ef`（根因修复 + 硬件看门狗兜底）、`5e6886d`（CI 两道守卫），推送 main |
| 2026-08-10 | CI [run 31356270778](https://github.com/yekingyan/qmk-config/actions/runs/31356270778) 全绿，11 步全 success，产物 `dolphin-vial-firmware`（含两个 uf2）已上传 |
| 2026-08-10 14:20 | **左右手均已刷入**，USB 枚举时刻 `14:18:32`，正常使用中 |
| 2026-08-10 14:24 | 已连续 6 分钟无重新枚举 → 确认**看门狗没有误触发** |
| 2026-08-10 22:15 | **再次卡死**。枚举时刻仍是 `14:18:32`（存活 ≤7h57m）→ **看门狗未触发** → 主循环其实是活的 → PIO 串口死锁假设被否（当时称「分支 C」，即现在的「情况 B」）|
| 2026-08-10 22:25 | 现场判活：Space+Tab 进 _FUN 后按左上角，**`RPI-RP2` 正常弹出** → 主循环、矩阵扫描、`process_record_user`、切层逻辑全部活着 |
| 2026-08-10 23:03 | 补充证据：卡死时**专门试过 2 次 Vial 都连不上**；**console 在正常时确认能滚 log，卡死后一行不出** → 三条独立 USB IN 端点同时死 → **keymap 假设排除，定位为设备级 USB 发送通路** |
| 2026-08-11 00:18 | 提交 `4c21992`（两级恢复：远程唤醒 → `mcu_reset`），推送后 CI 全绿 |
| 2026-08-11 00:26 | 社区调研发现官方 API `restart_usb_driver()`，改为三级递进（`f3162cc`）|
| 2026-08-11 00:30 | **用户指出场景不符**：不是休眠唤醒，是正在打字时突然死。据此重查，发现 **RP2040 勘误 E15（USB 设备控制器硬件锁死）**，且 ChibiOS 驱动零 errata 处理 |
| 2026-08-11 00:37 | 发现原实现的致命盲区：硬件锁死时 `USB_DRIVER.state` 仍为 `USB_ACTIVE`，三级恢复一级都不会触发。加入硬件帧计数器检测（`0acbfeb`）|
| 2026-08-11 00:46 | 新增诊断脚本 `scripts/kb-diag.sh`（`dac1d34`）|
| 2026-08-11 00:43~01:00 | **左右手均已刷入 `0acbfeb`（PR #91 产物）**，含 keymap 新版 + USB 失声三级自恢复 + 硬件帧计数器检测 |
| 2026-08-11 01:00:25 | 刷完后重启电脑，产生本次枚举（顺序已与用户确认：先刷左右手 → 再重启电脑 → 才记基线）|
| 2026-08-11 01:01 | 记录基线：枚举时刻 `01:00:25`，D0，9 个接口全 OK，无卡住修饰键，无 USB/HID 错误、无休眠事件 → **长期验证期开始** |
| 2026-08-11 10:18:39 | **修复首次被证明有效**：使用中卡约 2 秒 ×2（间隔 20 秒），**均自动恢复、无需拔插**。9 个 USB 接口 `LastArrivalDate` 同时跳到 `10:18:39` → 整设备重新枚举 → **第 2 级 `restart_usb_driver()` 生效**，正是 E15 硬件锁死的预期表现。主机侧仍是零错误、零休眠事件 |
| 2026-08-11 10:20 | 查到 E15 的权威描述：*"USB Device controller will hang if **certain bus errors occur during an IN transfer**"*（TinyUSB `rp2040_usb.h`）→ 触发因子是**总线错误**，而非纯随机。解释了为什么会成簇出现 |
| 2026-08-11 10:34 | 用户补充：还卡了 1~2 次，**Windows 弹了「USB 不识别」**。据此查 PnP，在键盘同一端口（`Port_#0006.Hub_#0001`）上找到两个失败枚举节点（10:20:49、10:22:48）|
| 2026-08-11 10:50 | **定位到恢复逻辑自伤**：第 2 级每 3s 无条件重试，而 `restart_usb_driver()` 之后主机重新枚举期间 `state` 必然不是 `USB_ACTIVE` → 检测器恒判「死」→ 反复 `usbDisconnectBus()` 掐断枚举 → 弹窗。且 `stuck_since` 从不重置，第 3 级的 10s 从最初检测算起，会再叠一次 `mcu_reset()` |
| 2026-08-11 11:00 | 修复：加 5s 宽限期 + 第 2 级每周期只做一次（`grace_since` / `restart_tried`）。本地编译两个键盘均通过，反汇编对比 text 50620 → 50684（+64B）确认改动进了二进制。**尚未刷入设备** |
| 2026-08-11 11:40~12:10 | **落地一批优化**（本地完成，未刷入）：门控拆两档；诊断计数器 + `USB_DIAG` 键码；`dolphin52.c`/`dolphin54.c` 合并成 `users/vial/dolphin5x.c`；撤掉 `serial_vendor.c` 覆盖与两道旧 CI 守卫（换成 dolphin5x.c 编译守卫）；`kb-diag.sh` 加失败枚举检测 + 事件历史 + `--watch` |
| 2026-08-11 11:57 | 新脚本刚跑通就抓到又一次自伤：**11:57:21 枚举失败 → 11:57:32 才成功枚举**（设备上仍是有缺陷的 `0acbfeb`）|
| 2026-08-11 13:00 | 第 3 级从 `mcu_reset()`（=`NVIC_SystemReset()`，**不复位外设**）改为 `watchdog_reboot(0,0,0)`（经 `psm_hw->wdsel` 复位「除 ROSC/XOSC 外的一切」，**含 USB 外设块**）；诊断计数器的软复位判据从 `HAD_POR` 换成语义确定的 `watchdog_hw->reason` |
| 2026-08-11 13:10 | 实施 **Sticky master**：覆盖 `is_keyboard_master_impl()`，软复位后用 `scratch[1]` 沿用主手身份，消除「复位后枚举超时 → 判成从手 → `usb_disconnect()` → 设备从主机消失」这条恶性路径。反汇编确认覆盖生效（`nm` 里只有一个符号） |
| 2026-08-11 13:17 | 提交 `bf174e4`（全部固件与结构改动）+ `843a677`（上游主从判定语义的定向 CI 守卫），推送 main |
| 2026-08-11 13:23:38 | **左右手均已刷入 PR #92 产物**。同时**键盘换到了 `Port_#0003`**（原 `Port_#0006`）—— 物理层变量一并改变 |
| 2026-08-11 13:28 | 记录刷机标记（`kb-diag.sh --flashed`）→ **长期验证期重新开始** |
| 2026-08-11 14:16 | **`USB_DIAG` 键验证通过**，读数 `USBDIAG a=0 b=0 heal=0 rst=0 mcu=0 st=255 up=3146s`。全零 = 刷机后 52 分钟内一次恢复都没触发过；`st=255` 是初始值（从未判定失活），与 `a=b=0` 自洽。**关键交叉验证**：`up=3146s` = 52m26s，而枚举时刻 13:23:38 + 52m26s = 14:16:04，与按键时刻吻合 → **固件自己的运行时长与主机侧 `LastArrivalDate` 一致，说明期间既没有 MCU 复位、也没有重新枚举** |
| 2026-08-11 14:17 | 挂上被动监测 `kb-diag.sh --watch 60`（`setsid` + `nohup`），已确认进程存活且完成过采样 |
| 2026-08-11 13:25 | 顺带修掉脚本一处误报：换端口后，脚本把新端口上 08-05 的旧「枚举失败」节点当成新证据报警。Windows 会把这类残留节点按物理端口长期留在 PnP 库里，所以新增 `--flashed` 标记，只有晚于刷机时刻的失败枚举才算证据 |

**观察窗口**：历史 MTBF 是 7~8 小时到几天。「几天没复现」不能算修好，至少要连续正常使用 **2~3 周**。

**观察记录**（存活时长 = 两次 `LastArrivalDate` 之差；用 `scripts/kb-diag.sh` 读取）

| 枚举时刻 | 结束时刻 | 存活时长 | 结束原因 | 生效级别 | 备注 |
|----------|----------|----------|----------|---------|------|
| 2026-08-10 10:02:20 | ~11:33 前 | ≤91 分钟 | 卡死，手动拔插 | — | 修复前 |
| 2026-08-10 11:42:20 | 14:18 前 | — | 主动拔插刷机 | — | 修复前 |
| 2026-08-10 14:18:32 | ~22:15 | **≤7h57m** | 卡死，手动拔插 | — | 已含看门狗 + 串口上界，**两者均无效** |
| 2026-08-10 22:26:42 | 2026-08-11 00:43 | ~2h17m | 刷机/拔插 | — | 仍是无失声修复的固件 |
| 2026-08-11 01:00:25 | 2026-08-11 10:18:39 | 9h18m（其中绝大部分是静置） | **失声，自动恢复** | **2**（约 2 秒）| **首次证明修复生效**。用户报「卡了 2 秒，2 次，间隔 20 秒」；9 个 USB 接口的 `LastArrivalDate` 全部同时变为 `10:18:39` → 整设备重新枚举 |
| 2026-08-11 10:18:39 | 2026-08-11 10:22:50 | ~4m11s | **失声 + 恢复逻辑自伤**，最终自动恢复 | 2（多次）| 中间夹了 **2 次失败枚举**（10:20:49、10:22:48），根因是第 2 级每 3s 重试打断主机枚举。已修复 |
| 2026-08-11 10:22:50 | 2026-08-11 11:02:32 | ~39m42s | **失声，自动恢复** | **2**（约 2 秒）| **干净的一次**：这次主机侧**没有**新增失败枚举节点（`&0&6` 节点的 arrival 仍是 `10:22:48`）→ 第 2 级一次成功。说明自伤缺陷是概率性的（枚举够快就不会被掐断）|
| 2026-08-11 11:02:32 | 2026-08-11 11:19:14 | ~16m42s | **失声，自动恢复** | 2 | — |
| 2026-08-11 11:19:14 | 2026-08-11 11:20:02 | **48 秒** | **自伤循环，实时抓到** | 2（多次）| 11:19:14 成功枚举 → **11:19:44 又一次失败枚举**（`&0&6` 节点 arrival 更新）→ 11:20:02 才再次成功。这就是「第 2 级每 3s 重试打断枚举」的现场录像 |
| 2026-08-11 11:20:02 | 2026-08-11 11:57:32 | ~37m30s | **失声 + 自伤**，最终自动恢复 | 2（多次）| **11:57:21 枚举失败 → 11:57:32 成功**。新版 `kb-diag.sh` 刚跑通就自动记下了这一条 |
| 2026-08-11 11:57:32 | 2026-08-11 12:51:02 | ~54m | 失声，自动恢复 | 2 | 脚本自动记下 |
| 2026-08-11 12:51:02 | 2026-08-11 13:23:38 | ~32m | **刷机（PR #92）** | — | 不是失声。同时把 USB 换到 `Port_#0003` |
| **2026-08-11 13:23:38** | 进行中 | — | — | — | **✅ 完整修复版（`843a677`）的观察起点。** 端口 `Port_#0003`。基线与刷机标记已于 13:28 记录 |

**修复前（`0acbfeb`）一上午的成绩单**：6 次成功重新枚举 + 4 次失败枚举，
间隔一度缩到 48 秒。其中**失败枚举全部是自伤**，这一版应当彻底消失 ——
**「刷机后再出现 FAILED-ENUM」是本次修复失败的最直接信号。**

**频率恶化，需要尽快刷入修复**：08-11 上午已记录 6 次成功重新枚举
（10:18:39、10:22:50、11:02:32、11:19:14、11:20:02、11:57:32）+ 4 次失败枚举
（10:20:49、10:22:48、11:19:44、11:57:21），间隔从 40 分钟缩到 48 秒。
其中失败枚举**全部是自伤**，属于可以立刻消除的部分。

**2026-08-11 上午完整事件序列**（用户体感 + Windows PnP 时间戳对齐后的最终版）

| 时刻 | 来源 | 事件 | 判读 |
|------|------|------|------|
| ~10:18:35 | 用户 | 卡约 2 秒 | 第 2 级：`LastArrivalDate` = `10:18:39` |
| ~10:18:55 | 用户 | 卡约 2 秒（距上次约 20 秒；用户确认「卡完只隔几秒就来找我」，而消息在 10:19:03 到）| **主机侧无任何痕迹** —— 既没有新枚举、也没有失败枚举节点 → 最可能是第 1 级 `usbWakeupHost()` 生效，即那次是 `USB_SUSPENDED` 而非 E15 |
| 10:20:49 | PnP | **枚举失败**，端口 6 生成 `Unknown USB Device (Device Descriptor Request Failed)` 节点 | 用户看到的「USB 不识别」弹窗。**是恢复逻辑自己掐断的枚举** |
| 10:22:48→49 | PnP | 同一节点再 arrival→removal 一次 | 又失败一次 |
| 10:22:50 | PnP | 键盘 9 个接口全部重新 arrival | 终于成功 |
| 10:29:44 | 脚本 | 连查 6 次（间隔 25 秒）稳定 | 簇结束 |

用户原话：「跟你说完又卡了 1~2 次，电脑还弹出来了一次 usb 不识别的弹窗」、「（每次）都只有几秒」。

**待确认**：10:20:49 → 10:22:50 这约 2 分钟里键盘是不是完全没反应？
- 若「是」→ 符合「按键活动门控关闭 ⇒ 恢复逻辑不动作 ⇒ 一直死着，直到用户重新按键」
- 若「不是，中间还能用」→ 说明还有别的机制，需要重查

**⚠️ 2026-08-11 上午出现事件成簇：10:18:39、10:22:50 两次确认枚举 + 用户体感 2 次卡顿，
集中在 ~4 分钟内**，而历史 MTBF 是「使用中 7~8 小时」。两种解释必须区分开：

**⚠️ 2026-08-11 上午出现事件成簇：10:18:39、10:22:50 两次确认枚举 + 用户体感 2 次卡顿，
集中在 ~4 分钟内**，而历史 MTBF 是「使用中 7~8 小时」。两种解释必须区分开：

| 解释 | 支持它的证据 | 反对它的证据 |
|------|-------------|-------------|
| A. 真实 E15 锁死成簇 | E15 的触发因子是「IN 传输期间的总线错误」，信号完整性一旦变差就会连发；「静置从不复发」也吻合 | 无 |
| B. 帧停滞检测误报，`restart_usb_driver()` 被白白调用 | 频率异常高 | 误报需要「主循环阻塞 >100ms **且** 阻塞前后帧号恰好相同」，后者概率仅 1/2048（帧号 11 位、每 1ms +1、2048ms 回绕）；若检测无脑误报则会每 2 秒重启一次，而不是几分钟一次 |
| **C. 恢复逻辑打断自己的枚举（已证实并修复）** | Windows 留下了两个失败枚举节点，端口号与键盘一致；代码里第 2 级每 3s 无条件重试，而枚举期间 `state` 必然不是 `USB_ACTIVE` | 这一条**只解释「一次失声被拖成几分钟的反复枚举」**，不解释最初那次锁死为什么发生（那仍是 A） |

**结论**：4 分钟内的混乱 = **1 次真实锁死（A）+ 恢复逻辑自伤把它放大（C）**。
C 已修复；A 仍待长期观察。

**判别 B 的一条硬逻辑**：恢复动作有「3 秒内有按键活动」门控，
所以**任何一次自动恢复都必然发生在用户正在打字的瞬间**，用户一定能感觉到那 ~2 秒。
因此「枚举时刻变了但用户完全没感觉」= 强烈指向别的原因（例如线材接触不良导致的
主机侧重新枚举），而不是本修复动作。→ **每次发现枚举时刻变化，务必问用户当时有没有卡顿感**。

**已排除的环境因素**：键盘**直连主板 USB 3.0 根 Hub 的 Port #6**（Intel USB 3.20 xHCI，
PCI 0:20.0），**中间没有任何外接 hub**（已查 PnP 父设备链）。所以不是 hub 供电/带宽问题。

**下一步建议（尚未实施）**：在 `dolphin5x.c` 里加一组 RAM 计数器并用一个 Vial 键码
`send_string` 打出来，一次性把上面的 A/B 之争定案：

| 计数器 | 回答什么问题 |
|--------|-------------|
| 条件 (a) 命中次数（`state != USB_ACTIVE`）| 是软件状态机卡住 |
| 条件 (b) 命中次数（帧号停滞）| 是 E15 硬件锁死 |
| 「检测到但 2 秒内自愈、未触发第 2 级」次数 | **这个数很大 = 检测器过于敏感**，即解释 B |
| `restart_usb_driver()` 实际调用次数 | 与用户体感的卡顿次数对账 |
| 卡住时的 `USB_DRIVER.state` 值 | 若是 (a)，区分 `USB_SUSPENDED`(5) 还是 `USB_READY`(2) |

RAM 静态变量足够统计第 2 级（`restart_usb_driver()` 不重启 MCU，变量不丢）；
若要跨第 3 级芯片复位存活，用 `watchdog_hw->scratch[0..3]`（未被 pico-sdk 占用；pico-sdk 只用 `[4..7]`）。

**2026-08-11 10:18 事件详情**（第一条有效证据）

- 用户体感：卡约 2 秒，发生 2 次，间隔约 20 秒，**两次都自己恢复了，没有拔插**
- 脚本读数：枚举时刻 `01:00:25` → `10:18:39`，9 个接口（3 个 USB 功能 + 5 个 HID 子设备 + 复合父设备）
  的 `LastArrivalDate` 全部同时变化，`LastRemovalDate` 全空 → 整设备重新枚举，非单接口重绑
- 主机侧：D0、9 接口全 `status=OK/problem=none`、无卡住修饰键、
  **系统日志零条 USB/HID 错误**、零条 Kernel-Power 42/107（没休眠）
- `Kernel-PnP/Configuration` 与 `Device Management` 两个日志近 12 小时**无任何记录** →
  Windows 对「已安装设备的重新枚举」不留痕，所以**主机日志无法用来数事件次数**，
  只能靠 `LastArrivalDate`（会被后一次覆盖）+ 用户体感
- 判读：**第 2 级 `restart_usb_driver()` 生效**。2 秒 = 100ms 帧停滞检测 + 2000ms 门限 +
  50ms 驱动重启 + 主机重新枚举，时间账完全对得上。不是第 1 级（那不会重新枚举），
  也不是第 3 级（那要 10 秒）
- **未能确定的点**：`LastArrivalDate` 只保留最后一次，所以只能确认 **1 次**重新枚举。
  两种可能：(i) 两次都是第 2 级，`10:18:39` 是第二次，用户过了 ~24 秒才来报；
  (ii) `10:18:39` 是第一次，第二次走的是第 1 级远程唤醒（不重新枚举）。
  区分办法：**问用户「第二次卡完到你发消息隔了多久」** —— 若明显超过 20 秒则是 (i)

**检查点记录**（同一枚举周期内的中途查验，用于攒证据）

| 查验时刻 | 已存活 | 枚举时刻是否变 | 用户体感 | 备注 |
|----------|--------|---------------|---------|------|
| 2026-08-11 10:07 | 9h07m | 未变 | 正常 | 整夜静置未打字。**这条基本不算证据**：用户明确指出「整夜、甚至整几天没用，回来都不假死」是一贯现状，故障只在使用中触发。Kernel-Power 42/107 均为零 → 电脑没睡、USB 总线全程活跃，但无按键活动 |
| 2026-08-11 10:19 | 0h00m30s | **变了**（01:00:25 → 10:18:39）| 卡 2 秒 ×2，间隔 20 秒，均自恢复 | 见上方「2026-08-11 10:18 事件详情」。**修复首次被证明有效** |
| 2026-08-11 10:26 | 0h03m48s | **又变了**（10:18:39 → 10:22:50）| **待确认** | 第三次事件。已把基线更新为 `10:22:50` |
| 2026-08-11 10:29:44 | 0h07m | 未变（连查 6 次，间隔 25 秒）| 正常 | 簇结束，恢复稳定 |

> **有效观察时长只计「实际使用时段」。** 静置（不打字）期间从不复发是本故障的一贯特征，
> 所以 wall-clock 存活时长会系统性高估。历史 MTBF「7~8 小时」指的是**使用中**的时长。
> 结案标准里的「连续 2~3 周」应按**实际正常使用的天数**计，静置日不计入。

> **追加新记录时**：填「生效级别」列（1=远程唤醒/枚举时刻未变，2=`restart_usb_driver`/约 2 秒，
> 3=整芯片复位 `watchdog_reboot`/约 10 秒，—=未恢复需手动拔插）。判读规则见文档顶部「接手指引」表格。

**现象**：正常使用中（**并非休眠唤醒场景**）忽然整机无响应，键盘 HID、Vial(raw HID)、
console **三条 USB IN 端点同时失效**，主机侧却显示设备 Status=OK / D0 / 无错误 / 不重新枚举，
历史上只有热插拔 USB 能恢复。存活时长通常 7~8 小时，有时几天，偶尔短到 1.5 小时以内
—— 概率性触发，非周期性。

**已确认的事实**（Windows 侧 PnP 查询 + vial-qmk 源码核对 + 现场实测）：

- 主机侧完全正常：`VID_594B/PID_D054` 各接口 Status=OK、ProblemCode 空、电源状态 D0，
  失声期间**没有重新枚举**（`LastArrivalDate` 不变），系统事件日志零条 USB/HID 错误。
  → 不是线材/驱动/选择性挂起/重启循环。
- **主循环是活的**（这一点曾被误判，务必注意）：硬件看门狗 8 小时未触发；且失声时
  Space+Tab 进 _FUN 层后按左上角 `QK_BOOTLOADER`，`RPI-RP2` 正常弹出 → MCU、矩阵扫描、
  `process_record_user`、切层逻辑全部正常。故障只在 USB 发送通路。
- 三条独立 IN 端点同时死，而它们共用 `usb_endpoint_in_send()` 入口那句
  `if (usbGetDriverStateI(...) != USB_ACTIVE) return false;` → 指向设备级 USB 异常。
- **主手没有任何看门狗**：`quantum/split_common/split_util.c` 的 `split_watchdog_task()` 条件是
  `if (!split_watchdog_done && !is_keyboard_master())`，`SPLIT_WATCHDOG_ENABLE` 明确只作用于从手。
  这解释了为什么历史上必须手动拔插；但**硬件看门狗对本故障同样无效**，因为主循环没停、狗一直在被喂。

### 根因首选：RP2040 勘误 E15（USB 设备控制器硬件锁死）

**2026-08-11 修正**：先前引用的社区 issue（#18591 休眠恢复、#19008 按键唤醒主机）
都是**休眠/唤醒**场景，与本故障「正在打字时突然失声」并不对口。而且原诊断有个薄弱处：
主机活跃时每 1ms 发一个 SOF，要凑出 `DEV_SUSPEND` 所需的 ≥3ms 空闲，在打字过程中很难发生。

真正对口的是 **RP2040-E15：USB 设备控制器会硬件锁死**。

**E15 的确切触发条件**（2026-08-11 查实，TinyUSB `src/portable/raspberrypi/rp2040/rp2040_usb.h` 里的原话）：

```c
// RP2040-E15: USB Device controller will hang if certain bus errors occur during an IN transfer.
```

这句话改变了对本故障的理解：**触发因子是「IN 传输期间发生总线错误」，不是纯随机**。推论：

- 我们的三条失效通路（键盘 HID、raw HID、console）**全都是 IN 端点** —— 正好落在 E15 的触发面上
- 总线错误（CRC / bit-stuffing / 数据序列错误）由信号完整性决定：线材、USB 口、
  是否经 hub、附近的 USB3 / 无线干扰。所以**故障率与物理层有关，且会成簇出现**
  —— 这直接解释了 2026-08-11 那次「20 秒内两次」：一段时间内总线错误密集，就连着锁死两次
- 也解释了为什么「静置不打字时从不复发」：没有 IN 传输就没有触发窗口
- **降低发生率的物理手段**（零代码成本，值得先试）：换 USB 口（优先直连主板后置口、
  避开 hub 和 USB3 口旁边）、换更短更好屏蔽的线、远离无线接收器

**为什么没法照搬官方缓解。** pico-sdk 1.5.0 起提供 `PICO_RP2040_USB_DEVICE_UFRAME_FIX`，
发布说明称「**This fix is required for correctness**」，但：

- 它只挂在 TinyUSB 构建上（`src/rp2_common/tinyusb/CMakeLists.txt:34`），
  而 QMK 用的是 ChibiOS 自己的 USB 驱动
- ChibiOS 的 RP2040 USB 驱动（`lib/chibios-contrib/os/hal/ports/RP/LLD/USBDv1/`）中
  **没有任何** errata / E15 / uframe 相关处理（已 grep 确认）
- 且 TinyUSB 那份实现是**专门针对双缓冲 Bulk IN 端点**的（`hw_endpoint.e15_bulk_in`
  只在 `transfer_type == TUSB_XFER_BULK && dir == IN` 时置位），做法是常开 SOF 中断、
  在 SOF ISR 里按微帧节奏补挂 buffer control。键盘只有 Interrupt IN 端点，
  **这套代码无法直接移植**。所以「反应式恢复」（当前方案）仍是可行度最高的路线
- 该勘误连数据手册都没收录（见 [pico-sdk#1260](https://github.com/raspberrypi/pico-sdk/issues/1260)）
- 现场证据：树莓派官方论坛专帖
  [Erratum E15 seen in field without VL805](https://forums.raspberrypi.com/viewtopic.php?t=374030)：
  *"the RP2040 USB hardware lockup described in Erratum E15 can be triggered under
  more conditions than those listed in the erratum"* —— 不限于文档里写的接 Pi 4/400(VL805)

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

**已做但被证明对本故障无效的处置**：

- RP2040 硬件看门狗：只能救「主循环停摆」，本故障主循环是活的，狗照喂 → 无效。**保留**，它兜的是另一类风险。
- `serial_vendor.c` 忙等上界：修的是真实上游隐患，但已证明不是本故障原因。
  **2026-08-11 已撤销**，理由见下。

**已撤销：`serial_vendor.c` 本地覆盖（2026-08-11）**

曾经的做法：把上游 `platforms/chibios/drivers/vendor/RP/RP2040/serial_vendor.c` 复制到
两个键盘目录下，给其中两处 `osalSysLock()` 内的无超时忙等加时间上界
（上界 = 8×11bit/230400bps 的 10 倍 ≈ 3819µs）。覆盖机制靠 VPATH：
`serial_vendor.c` 以**裸文件名**通过 `QUANTUM_LIB_SRC += serial_$(SERIAL_DRIVER).c` 加入编译，
而 `builddefs/build_keyboard.mk` 的搜索顺序是
`KEYMAP_PATH` → `USER_PATH` → `KEYBOARD_PATHS` → `COMMON_VPATH`，键盘目录先于平台驱动目录。
配套还有两道 CI 守卫（上游漂移 sha256 校验 + 覆盖生效校验）。

**为什么撤掉**：它修的隐患真实存在，但

1. 已证明**不是**「USB 失声」的原因（那类故障会让主循环停摆、看门狗必然触发，实测未触发）
2. 那个隐患恰好能被硬件看门狗兜住 —— 串口死锁 → 主循环停摆 → 4s 后狗复位
3. 代价却是：vendored 一个**上游时序敏感驱动**、两道 CI 守卫、以及长期的版本同步负担

反汇编已确认回落到上游版本：上界常量 `0x00000eeb`(3819) 已从两个二进制中消失。

若哪天真需要重新施加上界，做法记录在此以备复用：用 `osalOsGetSystemTimeX()` /
`osalTimeDiffX()`（X 后缀可在临界区内安全调用）给那两处忙等加时间上界，
超时后放弃本次分体事务 —— 协议层已能容忍单次失败。

**配置侧备选降概率手段**（未应用）：调低分体串口速率
`#define SELECT_SOFT_SERIAL_SPEED 2`（230400 → 115200）。给 PIO 更多时序余量，
但只降概率不消除死锁，且无证据表明异常与速率相关。
注意这个宏对 vendor/PIO 驱动**同样生效**（`serial_vendor.c` 包含 `serial_usart.h`，
后者把它映射成 `SERIAL_USART_SPEED`）。

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

**恢复动作**（三级递进，代价由轻到重）：

| 级别 | 触发时机 | 动作 | 对 E15 有效？ | 代价 |
|------|---------|------|--------------|------|
| 1 | 每 250ms（且尚未做过第 2 级），**窄门控 3s** | `usbWakeupHost()` | ✗（state 仍 ACTIVE，内部判断不通过）| 无 |
| 2 | 卡满 2s，**每个失声周期只做一次**，宽门控 10s | `restart_usb_driver()`，随后进入 5s 宽限期 | **✓ 外设块硬件复位** | 主机重新枚举，MCU 不重启 |
| 3 | 第 2 级做过 + 宽限期已过 + 累计卡满 10s + 曾 ACTIVE 过 | **`watchdog_reboot(0,0,0)`**（不是 `mcu_reset()`，理由见下）| ✓ | 等效自动拔插一次 |

**为什么第 3 级用 `watchdog_reboot()` 而不是 QMK 的 `mcu_reset()`**（2026-08-11 改）：

1. **复位更彻底。** RP2040 上 `mcu_reset()` 就是 `NVIC_SystemReset()`
   （`platforms/chibios/bootloaders/rp2040.c:16`），只复位处理器子系统，**不复位外设**。
   而 `watchdog_reboot()` → `_watchdog_enable()` 会先设：
   ```c
   // lib/pico-sdk/src/rp2_common/hardware_watchdog/watchdog.c:40
   // Reset everything apart from ROSC and XOSC
   hw_set_bits(&psm_hw->wdsel, PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS));
   ```
   **USB 外设块也在这个范围里**。既然首选根因是 E15 那种 USB 控制器硬件锁死，
   第 3 级理应用能真正复位 USB 块的那种复位。
2. **给出可靠的「这次是软复位」信号**：`watchdog_hw->reason != 0`，诊断计数器靠它跨复位累计。

`delay_ms = 0` 走 `CTRL.TRIGGER` 立即复位。不会误进 UF2：双击复位的 magic 在开机 500ms 后
已清零，而第 3 级最早也是开机十几秒后才可能触发。

**跨复位判据为什么用 `watchdog_hw->reason` 而不是 `HAD_POR`**：

| 判据 | 可靠性 |
|------|--------|
| `watchdog_hw->reason`（WATCHDOG_BASE + 0x08）| ✅ 复位值为 0（`WATCHDOG_REASON_RESET _u(0)`），上电必读到 0；看门狗复位置 TIMER/FORCE 位。**pico-sdk 自己的 `watchdog_caused_reboot()` 就依赖它跨复位保留** |
| `vreg_and_chip_reset->chip_reset` 的 `HAD_POR` | ❌ 软复位之后读什么，取决于该寄存器块本身是否也被 `psm_hw->wdsel` 那次复位带走 —— 源码里读不出确定答案，所以不用 |

反汇编核对（`LTO_ENABLE=no` 后看 `keyboard_post_init_kb`）：

```asm
ldr  r3, [pc, #24]   ; r3 = 0x40058000 = WATCHDOG_BASE
ldr  r2, [r3, #8]    ; WATCHDOG->REASON   (offset 0x08 ✓)
cmp  r2, #0
bne  ...             ; 软复位 → 保留计数
str  r2, [r3, #12]   ; SCRATCH0 = 0       (offset 0x0C ✓)
movs r0, #250 ; lsls r0, #4   ; 4000ms
bl   watchdog_enable
```
`housekeeping_task_kb` 里也确认是 `bl watchdog_reboot`，已无 `mcu_reset` / `NVIC_SystemReset` 调用。

> **⚠️ 第 2 级绝对不能反复重试（2026-08-11 血的教训）。** 见下方
> 「自伤：恢复逻辑打断自己的枚举」。

门控（2026-08-11 起按级别分档）：

| 门控 | 适用级别 | 防什么 |
|------|---------|--------|
| `DOLPHIN_USB_ACTIVITY_WINDOW_MS` = 3000：3 秒内有按键活动 | **仅第 1 级** | 主机真休眠、用户没在用时保持安静，不反复唤醒主机。语义严格对齐上游 `suspend_wakeup_condition()` |
| `DOLPHIN_USB_RECOVERY_WINDOW_MS` = 10000：10 秒内有按键活动 | **第 2/3 级** | 同上，但放宽。**为什么要放宽**：用户一发现失声就停手不按键，3 秒窗口一关自救压根不会启动 —— 操作手册里「拔线前先按几个键等 6 秒」就是为此打的补丁，现在固件侧自己扛住 |
| 本次上电按过键（`last_key_activity != 0`）| 全部 | 必须显式排除 0，否则 `timer_elapsed32(0)` 在开机头几秒会被误判成「刚有按键活动」 |
| `was_active`（本次上电曾 ACTIVE 过）| **仅第 3 级** | 接充电头（有 5V 无主机）时按键不会导致反复复位 |
| 时间门限 | 全部 | 短暂抖动不触发 |
| **恢复后 5s 宽限期（`grace_since`）** | 全部（宽限期内连第 1 级都不做）| **防止打断主机正在进行的枚举**。枚举全程 `state != USB_ACTIVE`，检测器恒判为「死」，不豁免就会自伤 |

**诊断计数器**（2026-08-11 加，用 Vial 里的 `USB_DIAG` 键码 `send_string` 打出来）：

```
USBDIAG a=3 b=12 heal=9 rst=4 mcu=1 st=4 up=1234s
```

| 字段 | 含义 | 怎么读 |
|------|------|--------|
| `a` | 条件 (a) `state != USB_ACTIVE` 命中次数 | 大 → 软件状态机卡住 |
| `b` | 条件 (b) 帧号停滞命中次数 | 大 → **E15 硬件锁死** |
| `heal` | 检测到失活但在第 2 级触发前就自愈 | **大 → 检测器过敏**，该调门限 |
| `rst` | `restart_usb_driver()` 调用次数 | 与用户体感的卡顿次数对账 |
| `mcu` | 第 3 级整芯片复位次数 | 跨复位累计，存在 `watchdog_hw->scratch[0]`（magic `0xD54C`；`watchdog_hw->reason == 0` 即上电复位，清零）|
| `st` | 最近一次判定失活时的 `USB_DRIVER.state` | 2=`USB_READY` 4=`USB_ACTIVE` 5=`USB_SUSPENDED`。配合 `a` 能定位卡在哪个状态 |
| `up` | 本次上电已运行秒数 | **拿它和主机侧的 `LastArrivalDate` 交叉验证**：两者吻合 = 期间既没有 MCU 复位、也没有重新枚举；`up` 明显小于「枚举时刻到现在」= 期间发生过芯片复位 |

**基线读数（2026-08-11 14:16，刷机后 52 分钟）**：

```
USBDIAG a=0 b=0 heal=0 rst=0 mcu=0 st=255 up=3146s
```

全零 = 一次恢复都没触发过；`st=255` 是初始值（从未判定失活），与 `a=b=0` 自洽。
`up=3146s` 与枚举时刻 `13:23:38` 相加正好是按键时刻 → 交叉验证通过。

> **怎么读这几个数（观察期每次报障都问一次）**
>
> | 读数特征 | 结论 |
> |---------|------|
> | `b` 涨、`a` 不涨、`st=4`(`USB_ACTIVE`) | **E15 硬件锁死**被证实 —— 状态机看着正常，只有帧号冻结 |
> | `a` 涨、`st=5`(`USB_SUSPENDED`) | 软件状态机误检 DEV_SUSPEND |
> | `a` 涨、`st=2`(`USB_READY`) | 误检 BUS_RESET 被打回 READY 等主机重新配置 |
> | `heal` 远大于 `rst` | 检测器过敏：抖动就报，但都在 2 秒内自愈 → 该调大 `DOLPHIN_USB_FRAME_STALL_MS` |
> | `rst` 与用户体感的卡顿次数对不上 | 有恢复动作用户没察觉，或有卡顿不是 USB 引起的 |
> | `mcu` > 0 | 第 3 级动过 —— 说明第 2 级救不回来，是最需要关注的信号 |

为什么不用 console：`dprintf` 走的是同一条会被静默丢弃的 USB IN 通路，
恰好在需要它的时候没有输出（2026-08-10 实测：正常时能滚 log，失声后一行不出）。

### ⚠️ 自伤：恢复逻辑打断自己的枚举（2026-08-11 发现并修复）

**症状**：Windows 弹出「无法识别的 USB 设备」。

**证据**（Windows PnP 查询）：

```
USB\VID_0000&PID_0002\5&30741CDD&0&6
  FriendlyName = Unknown USB Device (Device Descriptor Request Failed)
  LocationInfo = Port_#0006.Hub_#0001     ← 与键盘完全同一个物理口
  install = 10:20:49    arrival = 10:22:48    removal = 10:22:49
键盘本体 USB\VID_594B&PID_D054\VIAL:F64C2B3C
  LocationInfo = Port_#0006.Hub_#0001
  arrival = 10:22:50                       ← 失败两次之后才成功
```

**根因（读代码确认，不是猜）**：`restart_usb_driver()` 之后主机要重新枚举，而
`USB_DRIVER.state` 要到 SET_CONFIGURATION 才会回到 `USB_ACTIVE`
（`usbStop()` → `USB_STOP`，`usbStart()` → `USB_READY`，配置完成 → `USB_ACTIVE`）。
也就是说**整个枚举过程中 `usb_is_dead()` 恒为真**。旧实现没有区分
「还没救回来」和「正在枚举」，于是：

1. 每 3 秒（`DOLPHIN_USB_RESTART_RETRY_MS`）再 `restart_usb_driver()` 一次
   → `usbDisconnectBus()` 把上拉拔掉，主机正在读描述符就被掐断
   → **Device Descriptor Request Failed + 弹窗**
2. `stuck_since` 从不重置，所以第 3 级的 10 秒是从**最初那次检测**算起
   → 前面刚重启过驱动，10 秒一到又叠加一次 `mcu_reset()`
3. 只要用户还在按键（门控开着），1 和 2 就循环，枚举可能几分钟都成不了

**修复**：第 2 级做完后设 `grace_since`，宽限期（`DOLPHIN_USB_RECOVERY_GRACE_MS`，
默认 5000ms）内**整段静默、连第 1 级都不做**，把总线完全交给主机；
每个失声周期第 2 级**只做一次**（`restart_tried`），宽限期过完还没活就直接升级到
第 3 级，而不是反复重启驱动。新的最坏时间线：
`检测 → 2s 重启驱动 → 5s 宽限 → 累计 10s 仍死则整芯片复位`。

**教训**：给「不健康」写自动恢复时，必须把「恢复动作本身造成的不健康」豁免掉，
否则恢复逻辑会和自己打架。

### 外部建议核实（2026-08-11，逐条查源码 + 查板子资料）

有人给了一份「YD-RP2040 分体假死」通用排查清单。**方向大体没错（都指向 USB/板子层面），
但具体机制大半对不上我们的配置。** 结论表如下，避免以后重复走一遍：

| 建议 | 核实结果 |
|------|---------|
| ①「VBUS 检测导致逻辑自杀：GP24 接了按键会电平波动，键盘误以为自己从主机变从机」 | **机制不成立**。GP24 = YD-RP2040 的 USRkey 用户按键（资料属实），但 ChibiOS 上 `SPLIT_USB_DETECT` **不读任何 GPIO**：`split_util.c:68` 的 `usb_bus_detected()` 轮询的是 `usb_connected_state()`，而它就是 `usbGetDriverStateI(&USB_DRIVER) == USB_ACTIVE`（`usb_util.c:25`）。更关键：主从只在 `split_pre_init()` 里判一次并缓存进 `split_config.master`，`is_keyboard_master()` 只返回缓存值 → **运行中不可能翻转** |
| ①-b「如果已有 `SPLIT_USB_DETECT` 就试着关掉」 | **做不到**。`platforms/chibios/chibios_config.h:19-21`：`#ifndef USB_VBUS_PIN` → `#define SPLIT_USB_DETECT`，即 ChibiOS 在没有专用 VBUS 引脚时强制打开。而 YD-RP2040 恰好「VBUS not available（有二极管挡着）」，接外部 VBUS_SENSE 很麻烦 —— 这也正是 Piantor 官方不推荐 YD-RP2040 的原因 |
| ①-c「EE_HANDS 完全不依赖引脚检测 USB 状态」 | **概念混淆**：EE_HANDS 管的是「我是左手还是右手」，主从判定仍然靠 USB 检测，没有替代关系。但**这条歪打正着有价值**，见下方「新发现的风险」 |
| ②「`SERIAL_DRIVER = vendor` 最稳」 | ✅ 我们本来就是（`rules.mk`）|
| ②-b「`SELECT_SOFT_SERIAL_SPEED` 设太快就降到 3/4」 | 机械上**成立**（我一开始以为对 vendor 无效，是错的）：`serial_vendor.c` 包含 `serial_usart.h`，后者把 `SELECT_SOFT_SERIAL_SPEED` 映射成 `SERIAL_USART_SPEED`，默认 1 = 230400bps。但对本故障**无意义**：分体串口通路已被排除（三条 USB IN 端点同时死，与分体链路无关；串口死锁会让主循环停摆、看门狗必触发，实测未触发）|
| ③ ESD / 静电干扰 | ✅ **方向正确且是补强**。E15 的触发因子正是「IN 传输期间的总线错误」，ESD/干扰就是总线错误的来源之一。这条与我们的根因判断同向 |
| ④ RGB 亮度 / 供电 brownout / `RGBLIGHT_LIMIT_VAL` | **不适用**。`RGBLIGHT_ENABLE = no` + `RGB_MATRIX_ENABLE = no`，本来就一颗灯都不亮（GP23 是 YD-RP2040 的 WS2812 脚，被我们当按键用，正是为此显式关掉的）|
| ⑤ 看门狗 / 任务堆积 / 自定义代码里的死循环 | **已做且已证明无效**。RP2040 硬件看门狗 4s 已启用，实测主循环没停、狗一直在被喂。唯一的无超时忙等在上游 `serial_vendor.c`，已本地覆盖加上界 |
| ⑥ 换短线 / 查 Type-C 座接触 | ✅ 有价值，与 E15 同向。已列为零成本先试项 |
| ⑦「开 `CONSOLE_ENABLE` 看假死前最后一刻打印什么」 | **明确行不通**。`dprintf` 自身也走 `usb_endpoint_in_send()`，状态一变日志同时被静默丢弃。实测：正常时 console 能滚 log，失声后一行不出 |
| ⑧ Vial dynamic keymap 写 Flash 导致短暂无响应 | **不适用**。不是持续写 flash 的场景；flash 写入是 ms 级，4s 看门狗不会触发；也不会误报帧号检测（阻塞前后帧号必然已推进）|
| ⑨「需要进一步区分 MCU 卡死 vs USB 栈异常」 | ✅ 说得对，但**这正是我们已经做完的事**。三条独立证据定案：看门狗未触发 + 失声时能进 bootloader + Vial/console 同时死 → MCU 活着，坏的是设备级 USB 发送通路 |
| ⑩ 换成官方 Pico 做对照实验 | 合理但要正确理解它能测什么：**E15 是全系 RP2040 的硅片勘误**（TinyUSB 对所有 `PICO_RP2040` 都启用 `CFG_TUSB_RP2_ERRATA_E15`），换官方 Pico **不能消除 E15**，只能改变发生率（ESD 防护、USB-C 座质量、信号完整性）。所以这个实验测的是「板子质量」，不是「有没有 E15」 |

> 补充：pico-sdk 还有个 **RP2040-E5**（*"USB device fails to exit RESET state on busy USB bus"*）
> 的缓解 `rp2040_usb_device_enumeration_fix()`，看着很像我们的「枚举失败」。但两条都用不上：
> 它只对 `rp2040_chip_version() == 1`（B0/B1 硅片）生效，**且实现方式是把 GPIO15 劫持成
> USB debug mux 来强制 LS_J** —— 而 GP15 在我们两个键盘里都是矩阵按键。

### ⚠️ 新发现的风险：第 3 级复位可能把键盘彻底打死

这是核实上面第 ①/①-c 条时顺带查出来的，**比原建议提的问题严重**：

```c
// quantum/split_common/split_util.c
bool is_keyboard_master_impl(void) {
    bool is_master = usb_bus_detected();      // 最多等 SPLIT_USB_TIMEOUT = 2500ms
    if (!is_master) { usb_disconnect(); }     // 判定为从手 → 直接关掉 USB
    return is_master;
}
bool is_keyboard_left_impl(void) {
    ...
#else   // 我们走的就是这个默认分支（MASTER_LEFT）
    return is_keyboard_master();              // 左右手身份 = 主从身份
#endif
}
```

也就是说 `MASTER_LEFT` 下 **「我是左手」这件事是从「我抢到了 USB」推导出来的**。
于是第 3 级复位有一条恶性路径：

1. `watchdog_reboot()` → 芯片重启 → `split_pre_init()` 重新判主从
2. 若重启后 **2500ms 内没能枚举到 `USB_ACTIVE`** —— 而我们**已经有实证说明枚举会彻底失败**
   （2026-08-11 主机侧留下多个 Device Descriptor Request Failed 节点）
3. 左半区判定自己是**从手 + 右手** → 调 `usb_disconnect()` 主动关掉 USB
4. 结果：设备从主机上**彻底消失**（比原来的「失声但设备还在」更糟），
   且两半都在等一个不存在的主手 → **只能拔插 USB**（好在还有 bootmagic，不至于拆机）

可选缓解：

| 方案 | 状态 | 说明 |
|------|------|------|
| **Sticky master** | ✅ **已实施**（2026-08-11，见下） | 彻底消除这条路径，且不依赖「枚举要在 2.5s 内成功」 |
| 调大 `SPLIT_USB_TIMEOUT`（2500 → 5000）| ❌ 否决 | 只降低概率；从手开机要多等 2.5s，用户明确说使用体验差 |
| 改用 `EE_HANDS` | ❌ 否决 | RP2040 上要先刷两个一次性镜像，且 bootmagic 会擦掉手别（详见「手别模式怎么选」一节）|
| 降低第 3 级触发频率 | ✅ 已做 | 宽限期修复让第 3 级只在「第 2 级失败」后才触发 |

#### ✅ 已实施：Sticky master（软复位后直接沿用主手身份）

把「复位前我是主手」记在 `watchdog_hw->scratch[1]`（pico-sdk 只用 `[4..7]`，`[0..3]` 空着；
scratch 跨看门狗复位保留、上电复位才清）。判定软复位用 `watchdog_hw->reason != 0`。

```c
bool is_keyboard_master_impl(void) {          // 覆盖上游的 weak 实现
    const bool soft_reset = (watchdog_hw->reason != 0);
    if (soft_reset && watchdog_hw->scratch[1] == DOLPHIN_MASTER_STICKY_MAGIC) {
        return true;                          // 沿用主手身份，完全跳过轮询
    }
    if (!soft_reset) watchdog_hw->scratch[1] = 0;   // 上电复位：清残留
    ... 复刻上游 usb_bus_detected() 的轮询 ...
    if (is_master) watchdog_hw->scratch[1] = DOLPHIN_MASTER_STICKY_MAGIC;
    else { watchdog_hw->scratch[1] = 0; usb_disconnect(); }
    return is_master;
}
```

语义正好对：

| 场景 | 行为 |
|------|------|
| 第 3 级芯片复位 / 4s 硬件看门狗复位（`reason != 0`）| 沿用主手身份，**顺带完全跳过那段最长 2500ms 的轮询** |
| 真拔插 = 断电 = 上电复位（`reason == 0`）| 清标志、走正常检测，行为与原来完全一致 |
| 从手 | 永远不写这个标志，判定不受影响 |

**⚠️ 维护负担**：`is_keyboard_master_impl()` 没有公开声明、`SPLIT_USB_TIMEOUT_POLL`（默认 10）
也只定义在 `split_util.c` 内部，所以 fallback 分支**复刻了上游 `usb_bus_detected()` 的逻辑**。
这与 2026-08-11 撤掉的 `serial_vendor.c` 属同一类做法（vendored 上游逻辑），
区别是这段是纯逻辑、不是时序敏感驱动，漂移风险低得多。

**已加 CI 守卫** `Guard upstream split master-detection assumptions`：
不做整文件 sha256（`split_util.c` 会因无关改动频繁变化，整文件哈希会天天误报），
只 grep 我们真正依赖的 6 条假设：

| # | 假设 | grep 的上游原文 |
|---|------|---------------|
| 1 | `is_keyboard_master_impl` 仍是 weak（我们才能覆盖）| `__attribute__((weak)) bool is_keyboard_master_impl(void)` |
| 2 | 轮询上限仍是 `SPLIT_USB_TIMEOUT / SPLIT_USB_TIMEOUT_POLL` | `for (uint16_t i = 0; i < (SPLIT_USB_TIMEOUT / SPLIT_USB_TIMEOUT_POLL); i++)` |
| 3 | 轮询的仍是 `usb_connected_state()` | `if (usb_connected_state())` |
| 4 | `SPLIT_USB_TIMEOUT_POLL` 默认仍是 10（我们本地也写 10）| `define SPLIT_USB_TIMEOUT_POLL 10` |
| 5 | 判定为从手时仍调 `usb_disconnect()` | `usb_disconnect();` |
| 6 | `is_keyboard_left_impl` 默认分支仍是「手别跟着主从走」| `return is_keyboard_master();` |

任一条失效就构建失败，并打印 `split_util.c` 的相关片段 + 提示同步
`users/vial/dolphin5x.c` 与复核本节推理。通过路径与两条失败路径（上游去掉 weak、
上游改掉轮询形态）都已本地实跑验证。

反汇编核对（`LTO_ENABLE=no`）：

```asm
is_keyboard_master_impl:
  r5 = 0x40058000        ; WATCHDOG_BASE
  ldr r3, [r5, #8]       ; REASON      (0x08 ✓)
  beq → str r3,[r5,#16]  ; reason==0 → scratch[1]=0
  ldr r2, [r5, #16]      ; scratch[1]  (0x10 ✓)
  cmp r2, 0x4d53544c     ; 'MSTL' magic ✓
  → movs r0,#1 ; return  ; sticky 生效
  movs r6, #250          ; 250 = SPLIT_USB_TIMEOUT(2500)/POLL(10) ✓
  bl usb_connected_state / chThdSleep(0x2710 = wait_ms(10))
  非主手 → scratch[1]=0 + bl usb_disconnect   ; 与上游一致 ✓
  主手   → scratch[1]=magic                   ; ✓
```

`nm` 里只有**一个** `is_keyboard_master_impl` 符号 → 上游 weak 版确实被替换掉了。
LTO 构建下 `0x4d53544c` 在两个键盘的二进制里各出现 2 次。

### 参考项目：geulpan42TP（YD-RP2040 + QMK + Split + TrackPoint）

来源：<https://arcreview.net/2024/03/yd-rp2040-with-qmk/>（韩文笔记，**无公开仓库**，只有配置片段）。
作者用 YD-RP2040 做带 TrackPoint 的分体键盘。原文给出的全部配置：

```make
# rules.mk
PS2_ENABLE       = yes      # TrackPoint
PS2_DRIVER       = vendor
PS2_MOUSE_ENABLE = yes
SERIAL_DRIVER    = vendor   # Split
WS2812_DRIVER    = vendor   # 板载 WS2812
RGBLIGHT_ENABLE  = yes
```

```c
// config.h
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U

#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#define WEAR_LEVELING_RP2040_FLASH_SIZE (PICO_FLASH_SIZE_BYTES)
#define WEAR_LEVELING_RP2040_FLASH_BASE ((WEAR_LEVELING_RP2040_FLASH_SIZE) - (WEAR_LEVELING_BACKING_SIZE))
#define WEAR_LEVELING_BACKING_SIZE 131072      // 4096 × 32，作者实测的上限
#define WEAR_LEVELING_LOGICAL_SIZE (WEAR_LEVELING_BACKING_SIZE / 2)
#define BACKING_STORE_WRITE_SIZE 2
```

对我们有用的四点：

| 要点 | 与我们的关系 |
|------|-------------|
| **板载 WS2812 占用 GP23，两者不能同时用**（原文：「내장 WS2812와 GP23을 동시에 사용할 수 없습니다」）| ✅ **独立佐证我们的设计**。Dolphin54 把 GP23 当最外侧拇指键，正因如此显式 `RGBLIGHT_ENABLE = no` + `RGB_MATRIX_ENABLE = no`。作者反过来选了用 RGB，所以他不能把 GP23 当按键 |
| **开了 `RP2040_BOOTLOADER_DOUBLE_TAP_RESET` 之后，物理「reset + bootsel」组合进不了 UF2 引导**，只能双击 reset | 参考价值有限：我们也开了这个宏（timeout `500U`，作者用 `1000U`），但**本键盘的 reset 按钮在壳里、要拆机才能按**，所以物理按钮那一套本来就用不上。我们进 bootloader 只走「Vial 改键 → 按键」，见「怎么进刷机模式」一节。真到了必须拆机的地步，记住此时 bootsel 组合无效、只能双击 reset |
| **flash 容量与 wear leveling** | 📌 **可改进项（与失声无关）**。QMK 自己**从不定义** `PICO_FLASH_SIZE_BYTES`（已 grep 全树确认，只有 `lib/pico-sdk` 的 board header 有），走 pico-sdk 默认 **2MB**；QMK 的 `WEAR_LEVELING_BACKING_SIZE` 默认 **8192**、`WEAR_LEVELING_LOGICAL_SIZE` = **4096**（`platforms/chibios/drivers/wear_leveling/wear_leveling_rp2040_flash_config.h:15-31`）。作者把 backing 提到 131072 后，Vial 宏内存从 **2387 → 63827 字节**。我们现在应该也被 4096 卡着 |
| **同型号不同批次 flash 芯片不同**：实测见过 W25Q128 与 25VQ128（都能刷 QMK，W25Q128 复制 uf2 更慢）| 「YD-RP2040 硬件一致性有批次差异」这条说法有实据 |

> **改 flash / wear leveling 前必须先搞清真实 flash 容量。** 两个风险：
> 1. `PICO_FLASH_SIZE_BYTES` 若设得**大于**实物容量，写入会落到 flash 之外。
>    `hardware_flash/flash.c:65` 的 `hard_assert(flash_offs + count <= PICO_FLASH_SIZE_BYTES)`
>    拦不住这种情况（它只按你声明的值校验），实际后果是数据损坏。
> 2. 改这些值会**移动 EEPROM 的物理位置** → 现有 Vial 配置（键位、combo）全部失效，
>    需要重刷 + Reset EEPROM，再靠 `keyboard_post_init_user()` 重新播种 combo。
>
> 判定真实容量：看板子上 flash 芯片丝印（W25Q32=4MB / W25Q64=8MB / W25Q128=16MB），
> 或临时刷一版把 JEDEC ID 打出来的固件。

**这篇对本次「USB 失声」没有直接帮助**：作者只记了配置与刷机踩坑，没有任何长期稳定性/假死记录，
而且他的 GP23 用途、RGB 开关、PS/2 外设都和我们不同，不构成对照实验。

### 社区调研：这是已知的上游问题，且没有修复

| 来源 | 内容 | 对我们的意义 |
|------|------|-------------|
| [树莓派论坛 t=374030](https://forums.raspberrypi.com/viewtopic.php?t=374030) | **E15 硬件锁死在现场被观察到，且触发条件比勘误记载的更宽**（不限于 VL805 主机）| **根因首选**。硬件锁死 + 只有复位能救 + 随机触发，与本故障吻合。2026-08-11 10:18 的实测（第 2 级复位救回）进一步支持 |
| [TinyUSB `rp2040_usb.h`](https://github.com/hathach/tinyusb/blob/master/src/portable/raspberrypi/rp2040/rp2040_usb.h) | E15 的权威一句话描述：*"USB Device controller will hang if certain bus errors occur during an **IN transfer**"* | **触发因子是总线错误 + IN 传输**，不是纯随机。我们死掉的三条通路全是 IN 端点；且能解释「20 秒内两次」的成簇现象与「静置时从不复发」。降频手段：改善物理层（换口 / 换线 / 避开 hub 与干扰源）|
| [TinyUSB `dcd_rp2040.c`](https://github.com/hathach/tinyusb/blob/master/src/portable/raspberrypi/rp2040/dcd_rp2040.c) | 官方缓解的实现：只对**双缓冲 Bulk IN** 端点（`e15_bulk_in`）生效，常开 SOF 中断并在 SOF ISR 里按微帧补挂 buffer control | 键盘只有 Interrupt IN 端点，**这套代码无法直接移植到 ChibiOS**。所以反应式恢复（当前方案）是可行度最高的路线，不是偷懒 |
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

### 怎么进刷机模式（本键盘有两条路，都不用拆机）

> **本键盘有两条进 bootloader 的路**，都不用拆机：
>
> 1. **Vial 改键 → 按键**（日常首选，顺手）
> 2. **Bootmagic：开机按住最外侧上角键**（兜底，不依赖 Vial，见上一节）
>
> 板载 reset / bootsel 按钮在壳里，只有「连矩阵都坏了」才需要拆机。

标准流程：

1. Vial 连上键盘（raw HID）
2. 把任意一个顺手的键改成 `QK_BOOTLOADER`（或先配好切层键，再走 `_FUN` 层左上/右上那两个）
3. 按下去 → 主机弹出 `RPI-RP2` 盘
4. 把 uf2 拖进去；左右手各做一次

**⚠️ 这条路依赖「Vial 能连上」。** 但**不是单点故障** —— 还有 bootmagic 兜底
（开机按住最外侧上角键，见上方「Bootmagic」一节）。两条路的分工：

| 情形 | 用哪条路 |
|------|---------|
| 日常改键位后要刷机 | Vial 改键 → 按 `QK_BOOTLOADER`（顺手） |
| 刚刷完机，EEPROM 被重置成默认键位，`_FUN` 进不去 | **bootmagic**：按住最外侧上角键插线 |
| 固件把 raw HID 弄坏了 / Vial 连不上 | **bootmagic**（这就是它存在的意义） |
| 连矩阵扫描都坏了 | 才需要拆机按 reset |

**因此刷机前后各有一条纪律：**

- **刷之前**：确认当前键位里有一个能按到的 `QK_BOOTLOADER`（走 `_FUN` 层也算）。
  忘了配也不要紧 —— 用 bootmagic
- **刷之后**：第一件事就是 Vial 配好切层键，把 `_FUN` 层的 `QK_BOOTLOADER` 重新变得可按

#### RP2040 上 EE_HANDS 到底能不能「统一固件」

**能，但要先用两个一次性镜像写入身份**（这里修正本文档早先「只能永远两个固件」的过度断言）：

1. 一次性：`make dolphin54:vial:uf2-split-left` 刷左手、`:uf2-split-right` 刷右手。
   这两个目标只是往编译里加 `-DINIT_EE_HANDS_LEFT/RIGHT`（`platforms/chibios/flash.mk:50-56`），
   开机时把手别写进 EEPROM
2. 之后：`EE_HANDS` 从 EEPROM 读手别，**左右手刷同一个统一固件**即可

手别能不能活过刷机与 Vial 重置：

| 操作 | `EECONFIG_HANDEDNESS`（eeconfig 偏移 14）会丢吗 |
|------|------------------------------------------|
| 刷 uf2 | **不丢**。uf2 只写固件区，模拟 EEPROM 在 flash 尾部（`PICO_FLASH_SIZE_BYTES - WEAR_LEVELING_BACKING_SIZE`）|
| Vial「Reset EEPROM」 | **不丢**。`id_eeprom_reset` 走 `eeconfig_init_via()`，只重置 dynamic keymap + 宏 + layout options |
| **Bootmagic（按住上角键开机）** | **会丢！** `bootmagic_reset_eeprom()` → `eeconfig_init_quantum()` → `nvm_eeconfig_erase()` → `eeprom_driver_format()` 整片格式化。之后两半都读到 0 = 「不是左手」→ **两半都以为自己是右手** → 矩阵镜像，得重刷一次 split-left/right 镜像才能救 |

**这一条决定了不要换 EE_HANDS**：bootmagic 是我们唯一不依赖 Vial 的 bootloader 兜底，
而 EE_HANDS 恰好把「用了 bootmagic 就得重做手别初始化」这个耦合引进来 ——
等于给唯一的救命通道加了一个副作用。现状 `MASTER_LEFT` 手别由 USB 检测推出、
**完全不存在 EEPROM 里**，所以 bootmagic 随便按，没有任何东西可丢。

> 备选兜底方案（**未采用，待定**）：给默认键位加一个 4 键 combo → `QK_BOOTLOADER`。
> 好处是它落在 `keyboard_post_init_user()` 的 combo 播种逻辑里（`entry.output == 0` 时自动补种），
> **刷完立刻可用、完全不需要 Vial，也不违反「默认不切层」的定案**（combo 不是切层键），
> 且 4 键同按不可能误触。代价是占一个 Vial combo 槽位。

### ✅ Bootmagic：不依赖 Vial、不用拆机进 bootloader（2026-08-11 查实）

**开机时按住最外侧上角键 → 该半区进 bootloader。** 这条路一直存在，只是之前没意识到。

`BOOTMAGIC_ENABLE` 在我们的构建里是开着的（cflags 里有 `-DBOOTMAGIC_ENABLE`），
调用链（`quantum/bootmagic/bootmagic.c`）：

```c
__attribute__((weak)) bool bootmagic_should_reset(void) {
    uint8_t row = BOOTMAGIC_ROW;      // 默认 0
    uint8_t col = BOOTMAGIC_COLUMN;   // 默认 0
    return matrix_get_row(row) & (1 << col);
}
__attribute__((weak)) void bootmagic_scan(void) {
    matrix_scan(); wait_ms(BOOTMAGIC_DEBOUNCE); matrix_scan();
    if (bootmagic_should_reset()) {
        bootmagic_reset_eeprom();
        bootloader_jump();            // ← RP2040 上是 reset_usb_boot()
    }
}
```

我们没有自定义 `BOOTMAGIC_ROW/COLUMN`，所以就是矩阵 `[0,0]`，
即 `_BASE` 层的 `KC_ESC` —— **最外侧上角那颗键**。

**为什么插哪半都好用**（关键在初始化顺序，`quantum/keyboard.c:469`）：

```
keyboard_init():
  via_init()
  split_pre_init()    ← 先定主从与手别（含最多 SPLIT_USB_TIMEOUT 的轮询）
  matrix_init()       ← 按 isLeftHand 选引脚映射，设 thisHand
  quantum_init()      ← bootmagic() 在这里
```

bootmagic 在主从/手别判定**之后**才跑，所以那时 `thisHand` 已经正确。
而 `MASTER_LEFT` 下手别 = 主从，**插 USB 的那半必然 `thisHand = 0`**，
自己的键就落在矩阵第 0~4 行 → `matrix_get_row(0)` 读到的正是它自己的上排 →
按住它的最外侧上角键就能命中 `[0,0]`。因为左右走线严格镜像，GP3 在两半都是最外侧上角键。

> **操作**：插 USB 的同时按住那半区的**最外侧上角键**，按住约 3 秒不放
> （要等过 `split_pre_init()` 里最多 2.5 秒的主从检测），`RPI-RP2` 就会弹出。
>
> 顺带会执行 `bootmagic_reset_eeprom()` 清空 EEPROM —— 对我们**无副作用**：
> Vial 配置反正每次刷机都会被清（`BUILD_ID` 随机），而我们没用 `EE_HANDS`，
> 手别不存在 EEPROM 里，所以没东西可丢。

**这条路把之前记的「单点故障」解掉了**：不再需要「Vial 能连上」才能进 bootloader。
即使刷进去的固件把 raw HID 弄坏了，只要矩阵还能扫，按住上角键重新插线就能救回来。
**从手（没插 USB 那半）按 bootmagic 是无效的**：一是它的键在矩阵 5~9 行而 bootmagic 查第 0 行
（除非定义 `BOOTMAGIC_ROW_RIGHT/COLUMN_RIGHT`），二是更根本的——它身上没有 USB 线，
就算进了 bootrom 也不会有 `RPI-RP2` 出现。**刷哪半，线就得插哪半。**

### 从手（右手）为什么按不出 bootloader，以及手别模式怎么选

**从手压根不处理键码。** `quantum/keyboard.c` 里：

```c
__attribute__((weak)) bool should_process_keypress(void) {
    return is_keyboard_master();     // 从手返回 false
}
...
if (process_keypress) { action_exec(MAKE_KEYEVENT(row, col, key_pressed)); }
```

从手只扫描矩阵、把原始状态经串口发给主手，`action_exec()` 在从手上**永不执行**。
所以在右手按 `QK_BOOTLOADER`，进 bootloader 的是**左手（主手）**。
**这与固件是否分左右无关**，是分体架构的固有行为。

要刷右手，只能把 USB 插到右手让它变成主手。

**好消息：现状下这样做键位是可用的。** 左右引脚映射严格镜像，GP → 物理角色在两半一致：

| 引脚 | 左手 | 右手 |
|------|------|------|
| GP3 | `[0,0]` 最外侧上排 | `[5,5]` 最外侧上排 |
| GP23 | `[4,2]` 外侧拇指 | `[9,2]` 外侧拇指 |
| GP17 | `[4,0]` 中拇指 | `[9,1]` 中拇指 |
| GP18 | `[4,1]` 内拇指 | `[9,0]` 内拇指 |

而 `quantum/matrix.c:276` 是 `if (!isLeftHand) { 用 direct_pins_right }`、`:299` 是
`thisHand = isLeftHand ? 0 : ROWS_PER_HAND`。右手插上 USB 后判定自己是主手，
`MASTER_LEFT` 下 handedness = master 所以它认为自己是左手 → 用左手引脚映射、键落在矩阵 0~4 行
→ **它就变成一块左手键盘**。因为镜像对称，每个物理键落到「左手的镜像同位」，
所以按右手的最外侧上排键正好是 `[0,0]` = `_FUN` 层的 `QK_BOOTLOADER`，切层拇指也在对应物理拇指上。

#### ⚠️ 网上那份「EE_HANDS 刷机指南」对 RP2040 不成立

常见说法是「EE_HANDS = 同一个固件，首次刷一次 `eeprom-lefthand.hex` 写入身份」。
**这是 AVR 专属流程**：`quantum/split_common/eeprom-lefthand.eep` 是 Intel HEX 格式的
AVR EEPROM 镜像（内容 `:0F000000...`），用 avrdude 写进 AVR **独立的** EEPROM 芯片区。
RP2040 没有独立 EEPROM（是 flash 里的 wear-leveling 模拟），这条路**不存在**。
QMK 源码里明写着：

```make
# platforms/chibios/flash.mk:48
# TODO: Remove once ARM has a way to configure EECONFIG_HANDEDNESS
#       within the emulated eeprom via dfu-util or another tool
ifneq (,$(filter $(MAKECMDGOALS), dfu-util-split-left uf2-split-left))
    OPT_DEFS += -DINIT_EE_HANDS_LEFT
endif
```

**所以 RP2040 上 EE_HANDS 的唯一做法就是编两个固件**
（`make xxx:uf2-split-left` / `:uf2-split-right`，本质是加 `-DINIT_EE_HANDS_LEFT/RIGHT`；
`EE_HANDS` 本身仍需写在 config.h 里）。

于是那张对比表的优缺点对我们是**反的**：

| 模式 | 网上说法 | 我们（RP2040）的实际情况 |
|------|---------|----------------------|
| `MASTER_LEFT`（**现状**）| 「分左右各一个固件」| ❌ 错。**一个固件**，两半刷同一个文件。手别由 USB 检测推出（`is_keyboard_left_impl()` 默认分支 = `return is_keyboard_master()`），**不落 EEPROM** |
| `EE_HANDS` | 「同一个固件，首次写一次 EEPROM」| 方向对，但实现不同：RP2040 没有 `.eep` 这条路，首次要刷 `uf2-split-left/right` 两个一次性镜像；之后才能统一固件。**代价是 bootmagic 会擦掉手别** |
| `SPLIT_HAND_PIN` | 「需要 PCB 支持」| ✅ 对。而且我们引脚基本用完（GP0-15 矩阵、GP16 串口、GP17-23 与 GP26-29 也都占了，只剩 GP24/GP25，而它们在 YD-RP2040 上是 USRkey 与 LED），还要两半接不同电平 |

**结论：现状（`MASTER_LEFT` + 单固件）已经是最省事也最稳的方案，不要换。** 三条诉求逐一对照：

| 诉求 | EE_HANDS 能解决吗 |
|------|-----------------|
| 「统一固件、升级只刷一个文件」| **现状本来就是这样**，没有提升 |
| 「免掉 2.5 秒等待」| ❌ **无效**。那 2.5s 是 `is_keyboard_master_impl()` 里的主从检测轮询，EE_HANDS 只改 `is_keyboard_left_impl()`。唯一能免掉的是 `USB_VBUS_PIN`（一次 GPIO 读），但 YD-RP2040 的 VBUS「有二极管挡着」拿不到，要飞线 |
| 「USB 插哪边都行」| ✅ **这是唯一的真实提升**：现状插右边也能用，但右半区会以左手身份工作（键位镜像）。不过 bootloader 键恰好在两半同一物理角色上，所以实操没差别 |
| 「减少出问题的情况」| 只减轻一半：消掉「误判 → 矩阵镜像」，但消不掉更严重的「误判 → `usb_disconnect()` → 设备从主机消失」。**同时新增**「用过 bootmagic 就得重做手别初始化」这个耦合 |

**真正对症的解法**（未做，待定）：用 QMK 的自定义分体 RPC
（`quantum/split_common/transactions.h` 的 `transaction_register_rpc` /
`transaction_rpc_send`）做一个键码：主手按下 → RPC 通知从手 → **从手自己调 `bootloader_jump()`**。
这样右手不用插拔线就能进 bootloader，线一直留在左手。约 30~40 行。

### 下次失声时怎么做（操作手册）

#### 情况 0：被动监测 —— 防止「悄悄恢复了但我们不知道」

修复生效后，第 1、2 级会在 2 秒内救回来，你可能只感觉「卡了一下」就过去了、根本想不到来报。
那这次数据就丢了。所以加一条零成本的被动检查：**跑诊断脚本**。

```bash
scripts/kb-diag.sh              # 查看当前状态、与基线对比，并自动记录新事件
scripts/kb-diag.sh --flashed    # 刷机后专用：记基线 + 打「固件刷入」标记
scripts/kb-diag.sh --baseline   # 只记基线（重启电脑后用这个）
scripts/kb-diag.sh --log        # 额外输出 USB/HID 错误与休眠唤醒事件
scripts/kb-diag.sh --history    # 打印事件历史（默认最近 20 条）
scripts/kb-diag.sh --watch 60   # 常驻盯着，每 60 秒采样，只在有新事件时输出
scripts/kb-diag.sh --quiet      # 只在有新事件时输出（给 cron / --watch 用）
```

> **刷机后一定用 `--flashed` 而不是 `--baseline`。** 原因：Windows 会把
> 「枚举失败」的残留节点（`USB\VID_0000&PID_0002`，FriendlyName 含
> *Device Descriptor Request Failed*）**按物理端口**长期留在 PnP 库里。
> 一换 USB 口就可能撞上那个口上几天前的旧记录 —— 2026-08-11 13:25 就踩到了：
> 键盘从 `Port_#0006` 换到 `Port_#0003` 后，脚本把该口上 08-05 的旧节点当成新证据报警。
> `--flashed` 会记下刷机时刻，之后只有**晚于它**的失败枚举才算证据。

**强烈建议观察期挂上 `--watch`**：

```bash
nohup scripts/kb-diag.sh --watch 60 >> scripts/.kb-watch.out 2>&1 &
```

理由：`LastArrivalDate` 只保留**最后一次**枚举，而 `Kernel-PnP/Configuration` 与
`Device Management` 两个日志对「已安装设备的重新枚举」**一条都不记**（实测近 12 小时零记录），
`USB-USBHUB3/Analytic` 默认关闭。所以不主动定时采样就只能靠人的体感 ——
2026-08-11 上午就因此漏掉了两次事件。

脚本一次抓齐：当前枚举时刻与已运行时长、电源状态（D0/D2/D3）、物理端口、
全部 USB 接口的 status/problemCode、**同一端口上的「枚举失败」残留节点**、
是否有卡住的修饰键、以及与基线的差异判读。

三个本地状态文件（都已 gitignore）：

| 文件 | 作用 |
|------|------|
| `scripts/.kb-baseline` | **给人看的**对比基准，刷机/重启后手动 `--flashed` / `--baseline` 重置 |
| `scripts/.kb-state` | **自动记录用的游标**：上次看到的枚举时刻、失败枚举时刻，以及 `flashed=` 刷机标记 |
| `scripts/.kb-events.log` | append-only 事件历史，`RE-ENUM` / `FAILED-ENUM` / `FLASHED` 三类记录 |

第 2、3 级恢复都会导致重新枚举，枚举时刻必然变化 —— 所以只要时刻变了而你没拔过线、
也没重启电脑，就说明期间发生过一次失声并被自动救回。配合 Vial 里的 `USB_DIAG` 键
还能把固件侧的计数打出来（字段含义见「已采用的修复」一节）。

> **注意**：刷机和重启电脑都会改变枚举时刻，所以这两件事之后都要重新 `--baseline`，
> 否则下次对比会误报。基线存在 `scripts/.kb-baseline`（已 gitignore）。

> **`LastArrivalDate` 只保留最后一次枚举，数不出次数。** 2026-08-11 实测：
> `Microsoft-Windows-Kernel-PnP/Configuration` 与 `.../Device Management` 两个日志
> 对「已安装设备的重新枚举」**一条都不记**（近 12 小时零记录），
> `Microsoft-Windows-USB-USBHUB3/Analytic` 默认关闭。所以多次事件的次数与间隔
> **只能靠用户体感**，务必一并记下「卡了几次、间隔多久、最后一次距你来报隔了多久」。
> 最后这一项能用来判断脚本读到的枚举时刻属于第几次事件。

脚本实现要点（踩过的坑）：WSL2 无 USB 子系统，必须绕道 `powershell.exe`；
PowerShell 输出是 CRLF，**必须 `tr -d '\r'`**，否则每个字段尾部带回车会让算术运算报错、
空值判断失效（第一版就栽在这里，「卡住修饰键」全部误报）。

#### 何时该找我

- 打字时突然完全没反应 —— 不论后来是否自己好了
- 感觉「卡了一下」又恢复了（这正是修复生效的样子，**务必报**）
- 键盘无故断开重连、或反复断连
- 主机休眠唤醒后异常

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
| **Windows 弹「无法识别的 USB 设备」** | **恢复逻辑打断了自己的枚举**（第 2 级反复重试）| 已于 2026-08-11 修复（5s 宽限期）。若刷入新固件后仍出现，说明 5s 不够，调大 `DOLPHIN_USB_RECOVERY_GRACE_MS`。核实方法见「自伤」一节的 PnP 查询 |
| 枚举时刻频繁变化、时不时断一下 | 复位兜底误触发 | `DOLPHIN_USB_STUCK_RESET_MS` 从 10000 调大 |
| 主机休眠唤醒后异常 | 意外——本方案未改动任何 suspend 配置 | 立刻报 |
| 打字莫名丢字 | 不该发生，恢复逻辑不碰正常路径 | 报 |

#### 顺带：keymap 新版刷完要验证

> **⚠️ 先搞清楚一件事：每次刷机都会把 Vial 配置清回默认键位。**
>
> `via_eeprom_is_valid()`（`quantum/via.c:86`）在 `VIAL_ENABLE` 下拿 EEPROM 里的
> magic 与 **`BUILD_ID`** 比对，而 `BUILD_ID` 是 `util/build_id.py` 里
> `random.randrange(0, 2**24 - 1)` **每次构建随机生成**的
> （实测连续三次增量构建得到 `0x00C49231` / `0x0088E25D` / `0x009457F3`，全不一样）。
> 校验失败 → `via_init()` 调 `eeconfig_init_via()` → `dynamic_keymap_reset()`
> 把 EEPROM 键位重置成编译进去的默认键位，并 `dynamic_keymap_macro_reset()` 清空宏。
>
> 推论（**修正了本文档以前的错误说法**）：
> - 「刷前先在 Vial 里配好」是**没用的** —— 配置会被刷机清掉，必须**刷完再配**
> - 好处是：改默认键位（比如新加的 `USB_DIAG`）**不需要手动 Reset EEPROM**，刷完自动生效
> - combo **不受影响**：`keyboard_post_init_user()` 里 `if (entry.output == 0)` 会自动补种
>
> **而默认键位里一个切层键都没有 —— 这是既定设计，不是缺陷**（见「状态」一节里
> 「拇指切层」条目下的定案说明）。切层属于进阶用法，所以刷完的第一步就是
> **在 Vial 里配切层键**，这是正常流程。配好之前 `_FUN` / `_MEDIA` 进不去，
> `QK_BOOTLOADER` 与 `USB_DIAG` 也按不出来，直到配好切层键。
> **本键盘不能靠双击物理 reset 兜底**（reset 按钮在壳里，要拆机），见「怎么进刷机模式」一节。

刷完按这个顺序做（**第 1 步是正常流程，不是在补救什么**）：

1. **先在 Vial 里配切层键**：内侧四拇指 `LT(1,SPC)` / `LT(2,TAB)` / `LT(3,ENT)` / `LT(4,BSPC)`，
   外侧两拇指配成进 `_FUN`(5) / `_MEDIA`(6)
2. 验证内侧四拇指长按能进 _NAV / _NUM / _SYM / _MOUSE
3. 验证外侧两拇指能进 _FUN / _MEDIA
4. **验证 `USB_DIAG`**：进 `_FUN` 后按**左上角 `QK_BOOTLOADER` 正下方那颗键**
   （即 `_FUN` 层矩阵 `[1,0]`），光标处应敲出
   `USBDIAG a=0 b=0 heal=0 rst=0 mcu=0 st=255 up=NNNs`
5. Caps Word 下按 `-` 出 `_`、按数字出数字
6. J+K 仍能出 Shift（确认 combo 补种生效）

**结案标准**：连续正常使用 2~3 周无失声 → 勾掉状态项，把本节压缩成「已知坑」表里一行。


**分支 D — 出现新问题：右半区失灵 / 按键丢失 / 分体不同步**

**2026-08-11 起这条基本可以排除自家改动**：`serial_vendor.c` 的本地覆盖已撤销，
分体串口跑的就是上游原版，没有我们加的 3819µs 上界了。
所以真出现这类症状，先按普通分体问题查：TRRS 线接触/屏蔽、GP16 焊点、
以及 `SELECT_SOFT_SERIAL_SPEED`（默认 1 = 230400bps，可降到 2 = 115200 试试）。

**分支 E — CI 构建失败**

现在有两道守卫：

- `Guard upstream split master-detection assumptions` → **上游改了主从判定的语义**。
  我们在 `users/vial/dolphin5x.c` 里覆盖了 weak 的 `is_keyboard_master_impl()`（Sticky master），
  其 fallback 分支复刻了上游 `usb_bus_detected()` 的逻辑。日志会打出是哪一条假设失效、
  期望匹配什么，并附上 `split_util.c` 的相关片段。照着同步我们那份实现，
  同时复核「新发现的风险 / Sticky master」一节的推理是否仍成立。
  六条假设的清单见那一节。

  > 为什么不做整文件 sha256：`split_util.c` 会因无关改动频繁变化，
  > 整文件哈希会天天误报。这是从 `serial_vendor.c` 那道守卫学到的教训 ——
  > 那次是**整份复制**上游文件，所以整文件哈希是对的；这次只依赖几条语义，
  > 就该只校验那几条。

- `Verify shared users/vial/dolphin5x.c was compiled` → 共享文件没被编进去。
  这个守卫存在的理由是**失效是静默的**：编译照样成功，只是硬件看门狗、
  USB 失声三级自恢复、诊断计数器全部悄悄消失。检查顺序：
  1. 工作流的 `Copy keyboard definitions and userspace into vial-qmk` 步是否真的
     把 `users/vial` 拷进了 `vial-qmk/users/vial`
  2. keymap 名是否仍是 `vial`（QMK 的 userspace 机制靠「keymap 名 == `users/` 下的目录名」
     来决定是否把该目录加入 VPATH 并 include 它的 `rules.mk`）
  3. `users/vial/rules.mk` 里的 `SRC += dolphin5x.c` 是否还在

  本地复现构建环境的办法见下方「本地编译验证」。

### 诊断工具箱（本次排查中验证有效的手段，复用备查）

**首选 `scripts/kb-diag.sh`** —— 下面这些查询已全部封装进脚本，日常只用它即可。
以下保留原始命令，供脚本失效或需要临时变体时参考。

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

**判断故障层次的核心逻辑**（注意：这里的推论曾经出错，现已修正）：

关键在于**分层判活**，逐层往下排：

| 检查 | 若通过说明 |
|------|-----------|
| 硬件看门狗是否触发（看枚举时刻有无按 4s 周期跳变）| 未触发 ⇒ **主循环没停**，狗一直在被喂 |
| 失声时 Space+Tab 进 _FUN 后按左上角 `QK_BOOTLOADER`，`RPI-RP2` 是否弹出 | 弹出 ⇒ MCU、矩阵扫描、`process_record_user`、切层全部正常 |
| Vial(raw HID) 与 console 是否也死 | 也死 ⇒ 与 keymap / 层状态无关（这两条不经过 keymap），是设备级 USB 问题 |

**曾经犯过的错**：一度用「键盘 HID 与 raw HID 同时失效 ⇒ 主循环停摆」来推断，这是错的
—— 两者共用 `usb_endpoint_in_send()` 那道 `!= USB_ACTIVE` 的门，同时失效只能说明 USB 发送
通路坏了，说明不了主循环状态。另外也曾用「bootloader 能进」去排除「层卡住」，同样是错的
—— 它只能排除比 _FUN 更高的层，_FUN 自身卡住时左手拇指透传成 Space/Tab、左上角正是
`QK_BOOTLOADER`，观察完全吻合。

**本地编译验证**（仓库无 submodule，CI 才 clone vial-qmk；本地要自己拉）：

```bash
git clone --depth 1 -b vial https://github.com/vial-kb/vial-qmk.git /tmp/vial-qmk
cd /tmp/vial-qmk && git submodule update --init --depth 1 \
    lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf
# 键盘定义 + 共享的 users/vial（漏了后者会「编译成功但功能全丢」）
cp -r ~/projects/qmk-config/keyboards/dolphin5{2,4} keyboards/
mkdir -p users && cp -r ~/projects/qmk-config/users/vial users/
make dolphin54:vial && make dolphin52:vial
# 确认共享文件真被编进去了（CI 里的守卫查的就是这一行）
make dolphin54:vial 2>&1 | grep 'users/vial/dolphin5x.c'
```

**LTO 会吃掉符号，验证代码是否真的生成要看反汇编。** 三条踩过的坑：

1. `watchdog_enable`/`watchdog_update` 在 `nm` 里完全查不到（被内联），只能靠
   `objdump -d` 找字面量池核对，例如 `0x6ab73121`(watchdog magic)、
   `0x007a1200`(4000ms 的 load 值)。
2. **GCC 会把 `x < N` 编成 `x <= N-1`**，所以 grep 字面量要连 `N-1` 一起找。
   例：5000ms 的宽限期在二进制里是 `.word 0x00001387`(4999)，grep `0x1388` 一无所获。
3. 想看真符号就**关掉 LTO**：`make dolphin54:vial LTO_ENABLE=no`，
   然后 `objdump -d | awk '/<housekeeping_task_kb>:/,/^$/'` 能完整读出恢复逻辑，
   逐个核对门限常量。这是核对生成代码最可靠的办法。

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

- **进 bootloader 只有一条路：Vial 改键 → 按键。** reset / bootsel 按钮都在壳里，
  要拆机才能按 —— 详见上方「怎么进刷机模式」一节（含刷机前后各一条纪律）
- **只改键位**：只刷主手（插 USB 那半），从手无需重刷
- **改通信协议 / QMK 大版本升级**：两边都刷
- **每次刷机都会把 Vial 配置清回默认键位**（`BUILD_ID` 每次构建随机 → VIA magic 必然失配），
  所以刷完要重新在 Vial 里配切层键；combo 会自动补种，不用管

### 键位需求（对齐 zmk-config 设计）

> 34 键核心区必须与 `~/projects/zmk-config/docs/keymap-design.md` 完全一致。

- **Nav 层右手 Q 行**：`C(←) C(D) C(U) C(→) DEL`（按词跳跃 / Vim 翻页），使用标准 QMK `C()` 宏
- **Nav-Mac 层**：Mac 对应键使用标准 `G()`/`A()` 宏（⌘剪贴板 + ⌥词跳）
- **OSM 粘滞修饰**：QMK 内置 `OSM()` 在 LT 激活层上有 bug（独立按键发送），改用自定义 `SK_LGUI/LALT/LCTL/LSFT`
  - 行为对齐 ZMK `&skn`：chain 累加（多个修饰可叠加），1s 超时释放，重复按只刷新计时不 toggle-off
  - **释放时机 = 下一个输出键「按下」之后立刻放**（ZMK 里叫 `quick-release`），实现在
    `post_process_record_user()`。**不能等那个键松手**：打字必然滚动重叠，
    「粘滞 Shift → 打 cap」的事件序列是 按c→按a→松c→松a，等松手放 Shift 会让 `a` 也带上 Shift，
    打出 `CAp`（2026-08-11 修）。修饰键本身不消费粘滞状态，所以 `SK_LCTL → SK_LSFT → P`
    仍是 Ctrl+Shift+P。**只改了 dolphin54，dolphin52 未同步**（2026-08-11 用户决定先不管，
    dolphin52 还没刷机验证；哪天要动它，照搬 `dolphin54/keymaps/vial/keymap.c` 里的
    `sticky_consumed_by()` + `post_process_record_user()`，并删掉 `process_record_user()`
    末尾那段「松手时清」）
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
| 粘滞 Shift 点一下后打 `cap` 出 `CAp`（前两个字母都大写） | 粘滞修饰原来在「下一个键**松手**时」才释放，而打字必然滚动重叠：按c→**按a**→松c→松a，`a` 按下时 Shift 还在 | 改成「下一个输出键**按下**之后立刻释放」，钩子挂 `post_process_record_user()` —— 它在 `process_record_handler()` 之后执行（`quantum/action.c:298-299`），此时目标键的 HID 报文已带修饰键发出，所以只影响后面的键。等价于 ZMK sticky-key 的 `quick-release`。**修饰键不消费粘滞状态**（`IS_MODIFIER_KEYCODE` 本就落在 `IS_BASIC_KEYCODE` 区间外），Ctrl+Shift+P 报文序仍是 `Ctrl↓Shift↓P↓ → Shift↑Ctrl↑`，快捷键在 P 的 keydown 上触发 |
| 使用中偶发整机假死（键盘 HID + raw HID + console 三条 IN 端点同时死，只有拔插能恢复） | 首选 **RP2040 勘误 E15**：*"USB Device controller will hang if certain bus errors occur during an IN transfer"*，ChibiOS 驱动零 errata 处理；次要候选是设备级 `USB_DRIVER.state` 卡在非 `USB_ACTIVE`，`usb_endpoint_in_send()` 入口静默丢弃全部报文 | 已采用方案 2：`dolphin5x.c` 中「状态非 ACTIVE 或帧号停滞 >100ms」+ 按键活动门控 → `usbWakeupHost()` → `restart_usb_driver()`(2s，只做一次) → 5s 宽限 → `mcu_reset()`(累计 10s)。硬件看门狗对此**无效**（主循环活着，狗照喂）|
| 自动恢复反而让主机弹「无法识别的 USB 设备」 | 第 2 级 `restart_usb_driver()` 之后主机要重新枚举，而 `USB_DRIVER.state` 要到 SET_CONFIGURATION 才回 `USB_ACTIVE` → 枚举全程被检测器判为「还是死的」→ 每 3s 再 `usbDisconnectBus()` 一次，把枚举掐断；`stuck_since` 又从不重置，10s 到了还叠一次 `mcu_reset()` | 恢复动作后设 5s 宽限期（`DOLPHIN_USB_RECOVERY_GRACE_MS`）整段静默，且第 2 级每个失声周期只做一次（`restart_tried`），宽限期过完仍死才升级到第 3 级。**通用教训：自动恢复必须豁免「恢复动作本身造成的不健康」** |
| 排查时误用「bootloader 能进」排除 keymap 层卡住 | 该论证只能排除比 _FUN 更高的层；_FUN 自身卡住时左手拇指透传成 Space/Tab、左上角正是 `QK_BOOTLOADER`，观察完全吻合 | 用不经过 keymap 的通路判别：Vial(raw HID) 与 console 是否也死 |
| `serial_vendor.c` 忙等上界（**2026-08-11 已撤销**） | 修的是真实上游隐患，但已证明不是本故障原因 | 撤销理由：代价是 vendored 一个上游时序敏感驱动 + 两道 CI 守卫 + 版本同步负担，而该隐患恰好能被硬件看门狗兜住（串口死锁 → 主循环停摆 → 4s 后狗复位）。反汇编确认 `0x0eeb`(3819) 已从二进制消失 |
| 共享代码放 `users/vial/` 后，失效方式是**静默的** | QMK 的 userspace 机制只在「keymap 名 == `users/` 下目录名」时才把该目录加入 VPATH 并 include 其 `rules.mk`；一旦机制变了或 CI 忘拷 `users/`，构建照样成功，只是看门狗与 USB 自恢复全丢 | CI 守卫 `Verify shared users/vial/dolphin5x.c was compiled`：构建后 grep 日志确认它被编译 |
| 覆盖 weak 的 `is_keyboard_master_impl()` 时不得不复刻上游 `usb_bus_detected()` 的逻辑，上游改语义会静默失效 | 上游那个函数是 `static`、`SPLIT_USB_TIMEOUT_POLL` 也只定义在 `split_util.c` 内部，拿不到 | CI 守卫 `Guard upstream split master-detection assumptions`：只 grep 我们依赖的 6 条语义假设，**不做整文件 sha256**（`split_util.c` 会因无关改动频繁变化，整文件哈希会天天误报）|
| CI 只拷 `keyboards/`，不拷 `users/` | 早期工作流只 `cp -r keyboards/dolphinXX`，把共享代码搬到 `users/vial/` 后会漏 | 工作流的 `Copy keyboard definitions and userspace into vial-qmk` 步显式 `cp -r users/vial vial-qmk/users/vial` |
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
| **每次刷机都会把 Vial 键位配置清回默认** | `VIAL_ENABLE` 下 `via_eeprom_is_valid()`（`quantum/via.c:86`）拿 EEPROM magic 与 `BUILD_ID` 比对，而 `BUILD_ID` 由 `util/build_id.py` 的 `random.randrange(0, 2**24-1)` **每次构建随机生成**（实测连续三次增量构建全不一样）→ 必然失配 → `eeconfig_init_via()` → `dynamic_keymap_reset()` + `dynamic_keymap_macro_reset()` | 接受它并调整流程：切层键等自定义**刷完再配**（刷前配是白费）。好处是改默认键位不需要手动 Reset EEPROM。combo 不受影响，`keyboard_post_init_user()` 里 `entry.output == 0` 会自动补种 |
| 刷完发现 `_FUN` / `_MEDIA` 进不去，连 bootloader 键都按不出来 | **这是预期行为，不是 bug**：默认键位刻意一个切层键都没有（切层是进阶用法，见「状态」里的定案说明），而刷机又把 EEPROM 重置成默认键位 | 刷完在 Vial 里配切层键即可，这是正常流程。**不要建议「双击物理 reset」—— 本键盘的 reset 按钮在壳里，要拆机**，见「怎么进刷机模式」一节 |
| Vial UI 设置单键 LT 长按切层失效 | `update_tri_layer_state` 强制检查底层冲突 | 在 `layer_state_set_user` 中弃用原生 tri_layer 宏，完全由 `process_record_user` 内的自定义 `space_pressed` 兼容处理 |
| Vial 界面按键渲染错乱/拇指键不对齐 | vial.json 间距(x偏移)设置有误，或 keyboard.json 的 layout 数组未严格按物理坐标(从左到右)排序 | 检查并调整 vial.json 的偏移坐标 (如 `{"x":1}`)；keyboard.json 布局宏必须按实际界面展现的物理顺序书写 |
| `MASTER_LEFT` 下「左右手身份」等于「主从身份」，第 3 级芯片复位后若 2500ms 内枚举不成功，左半区会判自己是从手+右手并主动 `usb_disconnect()`，设备从主机上彻底消失 | `is_keyboard_left_impl()` 的默认分支就是 `return is_keyboard_master()`；而 `is_keyboard_master_impl()` 靠 `usb_bus_detected()` 最多等 `SPLIT_USB_TIMEOUT`(2500ms) 的 `USB_ACTIVE` | **已修**：Sticky master —— 覆盖 `is_keyboard_master_impl()`，软复位（`watchdog_hw->reason != 0`）时用 `scratch[1]` 里的 magic 沿用主手身份并跳过轮询；上电复位走原逻辑。详见「新发现的风险」一节 |
| 第 3 级用 `mcu_reset()` 复位得不够彻底 | RP2040 上 `mcu_reset()` 就是 `NVIC_SystemReset()`，只复位处理器子系统、**不复位外设**，而首选根因恰是 USB 控制器硬件锁死 | 改用 `watchdog_reboot(0,0,0)`：`_watchdog_enable()` 会设 `psm_hw->wdsel = PSM_WDSEL_BITS & ~(ROSC|XOSC)`，原注释「Reset everything apart from ROSC and XOSC」，**USB 块在内** |
| 想判断「这次是上电复位还是软复位」时用错了寄存器 | `vreg_and_chip_reset->chip_reset` 的 `HAD_POR` 在软复位后读什么，取决于该块是否也被 `wdsel` 复位带走，源码里读不出确定答案 | 用 `watchdog_hw->reason`：复位值为 0，上电必读 0；pico-sdk 自己的 `watchdog_caused_reboot()` 就依赖它跨复位保留 |
| `SELECT_SOFT_SERIAL_SPEED` 以为只对 bitbang 生效 | 实际上 `serial_vendor.c` 包含 `serial_usart.h`，后者把它映射成 `SERIAL_USART_SPEED`（默认 1 = 230400bps），所以对 vendor/PIO 驱动**同样生效** | 记住这一点，别再误判「这个宏对我们没用」 |

### 文件结构

```
users/vial/                    # 两个键盘共享（QMK userspace：keymap 名 == 目录名时自动生效）
├── dolphin5x.c                # 硬件看门狗 + USB 失声三级自恢复 + 诊断计数器
├── dolphin5x.h                # dolphin_usb_diag_report() 声明与输出字段说明
└── rules.mk                   # SRC += dolphin5x.c

keyboards/dolphin52/
├── keyboard.json          # 键盘元信息 + 52键布局定义
├── config.h               # RP2040 双击复位 + 串口 + Direct Pin 矩阵
├── rules.mk               # GENERIC_RP_RP2040 board + PIO serial driver
└── keymaps/vial/
    ├── keymap.c           # 7层键位 (核心Sweep + 外围传统) + USB_DIAG 键码
    ├── config.h           # Vial + Tap-Hold + OSM + Mouse 配置
    ├── rules.mk           # Vial/VIA + Combo + Caps Word
    └── vial.json          # Vial GUI 布局描述 + customKeycodes

keyboards/dolphin54/       # 同上结构，54 键
```

> **注意两件事：**
> 1. `users/vial/` 只对「keymap 名 = vial」的键盘生效。本仓库正好只有 dolphin52/54
>    是 vial keymap（ferris 用 yekingyan）。失效方式是**静默的**（编译成功但看门狗与
>    USB 自恢复全丢），所以 CI 里有专门的守卫 `Verify shared users/vial/dolphin5x.c was compiled`。
> 2. **CI 必须把 `users/vial` 拷进 vial-qmk**，见工作流的 `Copy keyboard definitions and userspace` 步。
> 3. 键盘目录下曾经有过 `serial_vendor.c`（vendored 上游驱动），**2026-08-11 已删除**。

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

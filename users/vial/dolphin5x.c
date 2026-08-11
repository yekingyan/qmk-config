// Copyright 2026 yekingyan
// SPDX-License-Identifier: GPL-2.0-or-later

/* dolphin5x.c —— Dolphin52 与 Dolphin54 共享的键盘级代码
 *
 * 位置说明：放在 `users/vial/` 而不是各自的键盘目录，是为了消除重复。
 * 原先 `keyboards/dolphin52/dolphin52.c` 与 `keyboards/dolphin54/dolphin54.c`
 * 字节级完全相同，靠手工 cp 同步、CI 里没有任何守卫，迟早漂移。
 * QMK 的 userspace 机制会在 keymap 名为 `vial` 时自动把本目录加入 VPATH，
 * 由 `users/vial/rules.mk` 的 `SRC += dolphin5x.c` 编入两个键盘。
 *
 * 这里定义的是 `*_kb` 级钩子，keymap 里定义的是 `*_user` 级钩子，互不冲突。
 */

#include "quantum.h"
#include "usb_main.h" // USB_DRIVER (== USBD1)
#include "usb_util.h" // usb_connected_state() / usb_disconnect()
#include "hardware/watchdog.h"
#include "dolphin5x.h"

/* ==========================================================================
 * 1) RP2040 硬件看门狗 —— 兜住「主循环卡死」类故障
 *
 * 背景：QMK 的 `SPLIT_WATCHDOG_ENABLE` 只保护从手。见 vial-qmk
 * `quantum/split_common/split_util.c` 的 `split_watchdog_task()`：
 *     if (!split_watchdog_done && !is_keyboard_master()) { ... mcu_reset(); }
 * 主手一旦主循环卡死就没有任何自恢复手段，只能手动拔插 USB。
 *
 * 这里启用 RP2040 硬件看门狗，在 housekeeping（每轮主循环都会跑）里喂狗。
 *
 * 注意事项：
 * - `watchdog_enable()` 的上限约 8388ms（24 位计数器，每微秒递减 2）。
 * - 复位走 bootrom 的常规 flash 启动路径（`watchdog_enable` 写的 scratch[4]
 *   magic 不是跳转 magic），不会误进 UF2 模式。
 * - 刷机用的 `bootloader_jump()` 走 bootrom 的 `reset_usb_boot()`，与看门狗
 *   互不干扰；双击复位的 magic 在开机 500ms 后已清零，而看门狗是在
 *   `keyboard_post_init_kb` 之后才启用的，不会误触发。
 * - 超时需留足余量给 EEPROM/flash 写入（Vial 改键、combo 默认值写入等）。
 * ========================================================================== */
#ifndef DOLPHIN_WATCHDOG_TIMEOUT_MS
#    define DOLPHIN_WATCHDOG_TIMEOUT_MS 4000
#endif

/* ==========================================================================
 * 2) USB 挂起卡死自恢复 —— 兜住「主循环活着但发不出报文」类故障
 *
 * 2026-08-10 实测确认的故障模式：
 *   现象  正常使用中忽然失声，键盘 HID 与 Vial raw HID 同时无响应，主机侧
 *         设备仍 Status=OK / D0 / 无错误 / 不重新枚举，只有拔插能恢复。
 *   证据  看门狗 8 小时未触发 + _FUN 层的 QK_BOOTLOADER 按下后 RPI-RP2 正常
 *         弹出 => 主循环、矩阵扫描、process_record 全都活着，死的是发送通路。
 *
 * 因果链（均已在 vial-qmk 源码中核对）：
 *   a. RP2040 USB 控制器只要看到 >=3ms 总线空闲就触发 DEV_SUSPEND 中断，
 *      `hal_usb_lld.c` 里 `_usb_suspend()` 把 `USBD1.state` 置为 USB_SUSPENDED。
 *   b. 此后 `usb_endpoint_in_send()`（tmk_core/protocol/chibios/usb_driver.c）
 *      入口处 `if (usbGetDriverStateI(...) != USB_ACTIVE) return false;`
 *      **静默丢弃每一份报文**。键盘 HID / raw HID / console 都走这里，一起死。
 *      主循环毫无察觉，狗照喂，所以硬件看门狗救不了这一类。
 *   c. 三条自愈路径全部不可用：
 *      - DEV_RESUME_FROM_HOST 中断：主机压根不认为设备挂起过，不会发 resume。
 *      - BUS_RESET：只有重新枚举（拔插）才有。
 *      - LLD 中 SOF 处理里的 `if (state == USB_SUSPENDED) _usb_wakeup()`：
 *        基线 `USB->INTE` 未开 `DEV_SOF`，该路径不通；按 `usb_lld_wakeup_host`
 *        宏的注释，SOF 中断只在发起远程唤醒时才被打开。
 *      - `usbWakeupHost()`：全代码库唯一调用点在 `chibios.c` 的
 *        `#if !defined(NO_USB_STARTUP_CHECK)` 块内，而我们定义了这个宏 => 被编译掉。
 *
 * 所以根源是 `NO_USB_STARTUP_CHECK`：它去掉了挂起时的阻塞（这是我们要的），
 * 同时也去掉了唤醒时的恢复（这是我们不想要的）。但不能简单删掉这个宏 ——
 * 那个阻塞循环内不调用 `housekeeping_task()`，会导致上面的硬件看门狗在主机
 * 休眠时每 4 秒复位一次。两者互斥。
 *
 * 因此这里非阻塞地补回恢复能力，语义对齐上游：
 *   - 仅在**有按键活动**时动作（对齐上游的 `suspend_wakeup_condition()`），
 *     避免主机真休眠、用户没在用时反复唤醒主机或复位刷屏。
 *   - 先节流调用 `usbWakeupHost()`。它内部自带 `state == USB_SUSPENDED` 判断，
 *     无条件调用是安全的；副作用是打开 DEV_SOF 中断，从而把上面 c 里那条
 *     SOF 自愈路径一并激活。
 *   - 仍恢复不了则整颗芯片复位兜底（`watchdog_reboot()`），等效于自动帮你拔插一次。
 *   - `was_active` 门控：只有「曾经正常工作过」才允许复位兜底，避免接充电头
 *     （有 5V 无主机）时按键导致反复复位。
 * ========================================================================== */

/* 按键活动的有效窗口：距上次按下多久之内算「用户正在用」。
 *
 * 分成两档（2026-08-11 加）。原因：门控存在的目的是**不去唤醒真正在休眠的主机**，
 * 而真会唤醒主机的只有第 1 级 `usbWakeupHost()`（以及第 2 级重新挂载总线）。
 * 但只用一个 3 秒窗口有个真实的可用性缺口：用户一发现失声就停手不按键，
 * 自救压根不会启动 —— 操作手册里那句「拔线前先按几个键等 6 秒」就是为此打的补丁。
 *
 * 所以第 1 级保持 3 秒（严格对齐上游 `suspend_wakeup_condition()` 的语义），
 * 第 2/3 级用更宽的窗口：它们要处理的是「已经确认坏了」的局面，宁可宽一点。
 */
#ifndef DOLPHIN_USB_ACTIVITY_WINDOW_MS
#    define DOLPHIN_USB_ACTIVITY_WINDOW_MS 3000
#endif
// 第 2/3 级用的宽窗口。设成比「用户察觉异常 → 停手 → 又按几下试试」这个
// 人类反应周期更长，避免停手就不自救。
#ifndef DOLPHIN_USB_RECOVERY_WINDOW_MS
#    define DOLPHIN_USB_RECOVERY_WINDOW_MS 10000
#endif
// 第 1 级：远程唤醒的重试节流间隔
#ifndef DOLPHIN_USB_WAKEUP_RETRY_MS
#    define DOLPHIN_USB_WAKEUP_RETRY_MS 250
#endif
// 第 2 级：卡这么久后重启 USB 驱动栈（官方 restart_usb_driver，不重启 MCU）
#ifndef DOLPHIN_USB_RESTART_AFTER_MS
#    define DOLPHIN_USB_RESTART_AFTER_MS 2000
#endif
// 第 2 级之后的宽限期：主机重新枚举要时间，这段时间内不得再判定失声。
// 见 usb_stuck_recovery_task() 里的详细说明（2026-08-11 踩过的坑）。
#ifndef DOLPHIN_USB_RECOVERY_GRACE_MS
#    define DOLPHIN_USB_RECOVERY_GRACE_MS 5000
#endif
// 帧计数器停止推进多久算「USB 硬件失活」。总线活跃时主机每 1ms 发一个 SOF，
// 所以 100ms 内一次都不动就绝非正常。
#ifndef DOLPHIN_USB_FRAME_STALL_MS
#    define DOLPHIN_USB_FRAME_STALL_MS 100
#endif
// 第 3 级：仍然救不回来就复位 MCU
#ifndef DOLPHIN_USB_STUCK_RESET_MS
#    define DOLPHIN_USB_STUCK_RESET_MS 10000
#endif

static uint32_t last_key_activity = 0;
static uint32_t stuck_since       = 0;     // 进入异常的时刻，0 = 正常
static uint32_t last_wakeup_try   = 0;
static uint16_t last_frame        = 0;
static uint32_t last_frame_move   = 0;     // 帧号最后一次变化的时刻，0 = 未知
static uint32_t grace_since       = 0;     // 第 2 级之后的宽限期起点，0 = 不在宽限期
static bool     restart_tried     = false; // 本次失声周期内是否已做过第 2 级
static bool     was_active        = false; // 本次上电后是否曾经 USB_ACTIVE

/* ==========================================================================
 * 3) 诊断计数器
 *
 * 为什么需要：console 抓不到这个故障（`dprintf` 自身也走 `usb_endpoint_in_send()`，
 * 状态一变日志同时被丢弃），所以只能把统计攒在 RAM 里，事后用一个键码打出来。
 *
 * 这几个数能把三种互相竞争的解释一次分开：
 *   - `cond_a` 大 → 软件状态机卡住（配合 `last_dead_state` 还能区分
 *     `USB_SUSPENDED`(5) 还是 `USB_READY`(2)）
 *   - `cond_b` 大 → E15 硬件锁死（state 仍 ACTIVE，只有帧号冻结）
 *   - `selfhealed` 大而 `restarts` 小 → 检测器过于敏感（抖动就报），需要调门限
 *   - `restarts` 与用户体感的卡顿次数对账；`resets` 是第 3 级动过几次
 *
 * `restart_usb_driver()` 不重启 MCU，所以普通 static 变量足够统计第 2 级。
 * 第 3 级的芯片复位会清零这些计数 —— 用 `watchdog_hw->scratch[0]` 跨复位
 * 累计 reset 次数（pico-sdk 只用 scratch[4..7]，[0..3] 空着）。
 * ========================================================================== */
static uint16_t diag_cond_a     = 0; // state != USB_ACTIVE 命中次数
static uint16_t diag_cond_b     = 0; // 帧号停滞命中次数
static uint16_t diag_selfhealed = 0; // 检测到失活但在第 2 级触发前就自己好了
static uint16_t diag_restarts   = 0; // restart_usb_driver() 实际调用次数
static uint8_t  diag_last_state = 0xFF; // 最近一次判定失活时的 USB_DRIVER.state

/* 跨复位存活的第 3 级计数。scratch[0] 高 16 位放 magic 做有效性校验，
 * 低 16 位是次数。
 *
 * 为什么用 `watchdog_hw->reason` 而不是 `vreg_and_chip_reset->chip_reset` 的 HAD_POR
 * 来区分「上电复位 / 软复位」：
 *   - `WATCHDOG_REASON` 的复位值是 0（`watchdog.h` 里 `WATCHDOG_REASON_RESET _u(0)`），
 *     上电必然读到 0；看门狗复位则置 TIMER 或 FORCE 位。
 *   - 这个「跨复位保留」的性质是 pico-sdk 自己在用的：`watchdog_caused_reboot()`
 *     就是 `watchdog_hw->reason && scratch[4] == WATCHDOG_NON_REBOOT_MAGIC`。
 *   - 反之 HAD_POR 在软复位之后读什么，取决于 vreg_and_chip_reset 块本身是否也被
 *     `psm_hw->wdsel` 那次复位带走，源码里读不出确定答案 —— 所以不依赖它。
 */
#define DOLPHIN_DIAG_SCRATCH_MAGIC 0xD54Cu
static bool dolphin_was_soft_reset(void) {
    return watchdog_hw->reason != 0;
}
static uint16_t diag_reset_count(void) {
    const uint32_t raw = watchdog_hw->scratch[0];
    return ((raw >> 16) == DOLPHIN_DIAG_SCRATCH_MAGIC) ? (uint16_t)(raw & 0xFFFFu) : 0;
}
static void diag_reset_count_bump(void) {
    watchdog_hw->scratch[0] = ((uint32_t)DOLPHIN_DIAG_SCRATCH_MAGIC << 16) | (uint16_t)(diag_reset_count() + 1);
}

/* ==========================================================================
 * 4) Sticky master —— 软复位后直接沿用主手身份
 *
 * 要解决的恶性路径（2026-08-11 读 split_util.c 发现）：
 *
 *   `MASTER_LEFT` 下「我是左手」这件事是从「我抢到了 USB」推导出来的 ——
 *   `is_keyboard_left_impl()` 的默认分支就是 `return is_keyboard_master();`。
 *   而 `is_keyboard_master_impl()` 靠轮询 `usb_connected_state()`，最长等
 *   `SPLIT_USB_TIMEOUT`(2500ms)。于是第 3 级芯片复位之后：
 *
 *     复位 → 主机重新枚举 → 若 2500ms 内没到 USB_ACTIVE
 *          → 本半区判定「我是从手 + 右手」
 *          → 上游会调 usb_disconnect()（usbDisconnectBus + usbStop）
 *          → **设备从主机上彻底消失**，且两半都在等一个不存在的主手
 *
 *   这比原来的「失声但设备还在」更糟。而我们**已有实证说明枚举会彻底失败**
 *   （2026-08-11 主机侧留下多个 Device Descriptor Request Failed 节点）。
 *
 * 解法：把「复位前我是主手」记在 `watchdog_hw->scratch[1]`（pico-sdk 只用 [4..7]，
 * [0..3] 空着；scratch 跨看门狗复位保留、上电复位才清）。判定软复位用
 * `watchdog_hw->reason != 0`，理由见上方 dolphin_was_soft_reset() 的注释。
 *
 * 语义正好对：
 *   - 软复位（第 3 级、或 4s 硬件看门狗）→ 沿用主手身份，**顺带完全跳过那段轮询**
 *   - 真拔插 = 断电 = 上电复位 → `reason == 0` → 清标志、走正常检测，行为与原来一致
 *   - 从手永远不会写这个标志，所以它那边的判定不受影响
 *
 * ⚠️ 代价：`is_keyboard_master_impl()` 没有公开声明，`SPLIT_USB_TIMEOUT_POLL`
 * 也只定义在 `split_util.c` 内部，所以下面的 fallback 分支**复刻了上游
 * `usb_bus_detected()` 的逻辑**。这和我们 2026-08-11 撤掉的 `serial_vendor.c`
 * 属于同一类做法（vendored 上游逻辑），区别是这段是纯逻辑、不是时序敏感驱动，
 * 上游漂移的风险低得多。若哪天上游改了主从判定方式，这里要跟着同步。
 * ========================================================================== */

// 与上游 split_util.c 的默认值保持一致
#ifndef SPLIT_USB_TIMEOUT
#    define SPLIT_USB_TIMEOUT 2000
#endif
#ifndef SPLIT_USB_TIMEOUT_POLL
#    define SPLIT_USB_TIMEOUT_POLL 10
#endif

#define DOLPHIN_MASTER_STICKY_MAGIC 0x4D53544Cul // 'MSTL'

// 上游把它声明在 split_util.c 内部，这里自己补一份原型（它是 weak，可被覆盖）
bool is_keyboard_master_impl(void);

bool is_keyboard_master_impl(void) {
    const bool soft_reset = dolphin_was_soft_reset();

    if (soft_reset && watchdog_hw->scratch[1] == DOLPHIN_MASTER_STICKY_MAGIC) {
        // 复位前就是主手 → 直接沿用，不轮询、不可能被误判成从手
        return true;
    }
    if (!soft_reset) {
        watchdog_hw->scratch[1] = 0; // 上电复位：清掉可能的随机残留
    }

    /* 以下复刻上游 `usb_bus_detected()` + `is_keyboard_master_impl()` 的行为 */
    bool is_master = false;
    for (uint16_t i = 0; i < (SPLIT_USB_TIMEOUT / SPLIT_USB_TIMEOUT_POLL); i++) {
        if (usb_connected_state()) {
            is_master = true;
            break;
        }
        wait_ms(SPLIT_USB_TIMEOUT_POLL);
    }

    if (is_master) {
        watchdog_hw->scratch[1] = DOLPHIN_MASTER_STICKY_MAGIC;
    } else {
        watchdog_hw->scratch[1] = 0;
        // 与上游一致：判定为从手就关掉 USB
        // （上游注释：Avoid NO_USB_STARTUP_CHECK - Disable USB as the previous checks seem to enable it somehow）
        usb_disconnect();
    }
    return is_master;
}

/* USB GET_STATUS(Device) 的 Remote Wakeup Enabled 位（USB 2.0 规范 9.4.5，bit1）。
 * 上游把它定义在 tmk_core/protocol/chibios/chibios.c 内部而非头文件里，
 * 所以这里按同样的值重新定义一份。 */
#ifndef USB_GETSTATUS_REMOTE_WAKEUP_ENABLED
#    define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED (2U)
#endif

/* 判断 USB 是否失活。两个条件取或，覆盖两类根因：
 *
 * (a) `USB_DRIVER.state != USB_ACTIVE` —— 软件状态机卡住。
 *     例如误检 DEV_SUSPEND 卡在 USB_SUSPENDED，或误检 BUS_RESET 被 _usb_reset()
 *     打回 USB_READY 等主机重新配置、而主机根本不知道发生过。
 *
 * (b) 硬件帧计数器停止推进 —— 对应 RP2040 勘误 E15：USB 设备控制器**硬件锁死**。
 *     这一条是必须的，因为硬件锁死后控制器不再产生任何中断，
 *     而 `_usb_suspend()` / `_usb_reset()` 只在中断里被调用，
 *     所以 `USB_DRIVER.state` 会一直停在 USB_ACTIVE —— 只看 (a) 完全检测不到。
 *     `usbGetFrameNumberX()` 读的是 `USB->SOFRD`（硬件寄存器），不依赖任何软件状态。
 *
 *     E15 背景：pico-sdk 1.5.0 起有官方缓解 PICO_RP2040_USB_DEVICE_UFRAME_FIX，
 *     发布说明称「required for correctness」，但它只挂在 TinyUSB 构建上；
 *     ChibiOS 的 RP2040 USB 驱动里没有任何 errata 处理（已 grep 确认），
 *     所以 QMK 拿不到这个修复。文档记载触发条件是接 Pi 4/400(VL805)，
 *     但树莓派官方论坛有「Erratum E15 seen in field without VL805」专帖。
 *
 * 注意 `USB->SOFRD` 是读清型寄存器（读它会清 SOF 中断标志）。基线 USB->INTE
 * 未开 DEV_SOF（实测为 0x0001d010），所以常态下轮询无害；但 usbWakeupHost()
 * 会打开 DEV_SOF，故这里只在 state == USB_ACTIVE 时才读，避开与 LLD 中
 * 基于 SOF 的自愈路径抢标志。
 */
static bool usb_is_dead(uint32_t now) {
    if (USB_DRIVER.state != USB_ACTIVE) {
        last_frame_move = 0; // 状态本身就不对，帧号无参考意义
        if (stuck_since == 0) {         // 只在「刚进入异常」时记一次，避免每轮主循环都累加
            diag_cond_a++;
            diag_last_state = (uint8_t)USB_DRIVER.state;
        }
        return true;
    }

    const uint16_t frame = (uint16_t)usbGetFrameNumberX(&USB_DRIVER);
    if (last_frame_move == 0 || frame != last_frame) {
        last_frame      = frame;
        last_frame_move = now ? now : 1;
        return false;
    }

    if (timer_elapsed32(last_frame_move) > DOLPHIN_USB_FRAME_STALL_MS) {
        if (stuck_since == 0) {
            diag_cond_b++;
            diag_last_state = (uint8_t)USB_DRIVER.state; // 应当是 USB_ACTIVE(4)，即 E15 特征
        }
        return true;
    }
    return false;
}

static void usb_stuck_recovery_task(void) {
    if (!is_keyboard_master()) {
        return; // 从手没有 USB 主机，这套逻辑不适用
    }

    const uint32_t now = timer_read32();

    if (!usb_is_dead(now)) {
        if (stuck_since != 0 && !restart_tried) {
            diag_selfhealed++; // 检测到失活，但还没升到第 2 级就自己好了
        }
        was_active    = true;
        stuck_since   = 0;
        grace_since   = 0;
        restart_tried = false;
        return;
    }

    /* ---- 恢复动作后的宽限期 ----------------------------------------------
     * 这一段是 2026-08-11 实测踩坑后补的，不能删。
     *
     * `restart_usb_driver()` 之后主机要重新枚举，而 `USB_DRIVER.state` 要到
     * SET_CONFIGURATION 才会变回 `USB_ACTIVE`。也就是说**整个枚举过程中
     * `usb_is_dead()` 恒为真**。原实现没有区分「还没救回来」和「正在枚举」，
     * 于是每 3 秒就再 `restart_usb_driver()` 一次，把一次本来会成功的枚举
     * 从中间掐断；更糟的是 `stuck_since` 从不重置，10 秒一到还会叠加一次
     * `mcu_reset()`。
     *
     * 主机侧的表现就是弹窗「无法识别的 USB 设备」。2026-08-11 10:20:49 与
     * 10:22:48 在 Windows 里留下了两个
     * `USB\VID_0000&PID_0002\5&...&0&6` → "Unknown USB Device
     * (Device Descriptor Request Failed)" 节点，端口号 6 与键盘的
     * `Port_#0006.Hub_#0001` 完全一致，即被掐断的正是本键盘的枚举。
     *
     * 所以第 2 级做完后必须整段静默，把总线交给主机，别再插手。
     * 宽限期内**连第 1 级都不做**（此时设备不可能是 USB_SUSPENDED）。
     * ------------------------------------------------------------------- */
    if (grace_since != 0) {
        if (timer_elapsed32(grace_since) < DOLPHIN_USB_RECOVERY_GRACE_MS) {
            return; // 枚举进行中，安静等着
        }
        grace_since = 0; // 宽限期过完还是死的 → 放行，继续升级到第 3 级
    }

    if (stuck_since == 0) {
        stuck_since = now ? now : 1; // 避开 0 这个「正常」哨兵值
    }

    /* 按键活动门控。用户没在按键就不动作：主机真休眠时保持安静。
     *
     * last_key_activity == 0 表示本次上电以来还没按过任何键 —— 必须显式排除，
     * 否则 timer_elapsed32(0) 在开机头几秒内会小于窗口值，被误判成「刚有按键活动」。
     *
     * 两档窗口见上方 DOLPHIN_USB_RECOVERY_WINDOW_MS 处的说明。 */
    if (last_key_activity == 0) {
        return;
    }
    const uint32_t idle_for      = timer_elapsed32(last_key_activity);
    const bool     wakeup_gate   = idle_for <= DOLPHIN_USB_ACTIVITY_WINDOW_MS;
    const bool     recovery_gate = idle_for <= DOLPHIN_USB_RECOVERY_WINDOW_MS;
    if (!recovery_gate) {
        return; // 连最宽的窗口都不满足，彻底安静
    }

    const uint32_t stuck_for = timer_elapsed32(stuck_since);

    /* 第 1 级：请求远程唤醒。最轻量，不打断任何东西。
     * usbWakeupHost() 内部自带 state == USB_SUSPENDED 判断，无条件调用是安全的；
     * 副作用是打开 DEV_SOF 中断，从而激活 LLD 中基于 SOF 的自愈路径。
     * 注意：若根因是 E15 硬件锁死（state 仍为 ACTIVE），这一级不会有任何作用。
     * 用窄窗口（wakeup_gate）：这一级是真的会去敲主机的那一下。 */
    if (wakeup_gate && !restart_tried && timer_elapsed32(last_wakeup_try) > DOLPHIN_USB_WAKEUP_RETRY_MS) {
        last_wakeup_try = now;
        if (USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED) {
            usbWakeupHost(&USB_DRIVER);
        }
    }

    /* 第 2 级：重启 USB 驱动栈。官方 API（tmk_core/protocol/chibios/usb_main.c:361，
     * 声明在 usb_main.h），做的是 usbDisconnectBus → usbStop → 停掉所有端点
     * → 等 50ms → 重新 init/start 所有端点 → usbStart → usbConnectBus。
     *
     * 关键：`usb_lld_start()` 在 state == USB_STOP 时会执行
     *   hal_lld_peripheral_reset(RESETS_ALLREG_USBCTRL) + unreset
     *   memset(USB, 0, ...) + memset(USB_DPSRAM, 0, ...)
     * 即**整个 USB 外设块硬件复位 + 寄存器与 DPRAM 清零**，与进 bootrom 时
     * bootrom 做的是同一类复位。所以它不仅能救软件状态卡住，
     * 也能救 E15 那种硬件锁死，而且**不重启 MCU** —— 层状态、粘滞修饰、
     * 分体链路、EEPROM 缓存全部保留。
     *
     * 每个失声周期只做一次：做完进宽限期把总线让给主机；宽限期结束还没活
     * 就直接升级到第 3 级，而不是反复重启驱动去打断枚举。 */
    if (!restart_tried && stuck_for > DOLPHIN_USB_RESTART_AFTER_MS) {
        restart_tried   = true;
        grace_since     = now ? now : 1;
        last_frame_move = 0; // 重启后帧号重新计基准
        diag_restarts++;
        restart_usb_driver(&USB_DRIVER);
        return;
    }

    /* 第 3 级：第 2 级做过、宽限期也过完了还是死的，才复位整颗芯片。
     * 等效于自动帮你拔插一次。
     * was_active 门控：只有「曾经正常工作过」才允许，避免接充电头（有 5V 无主机）
     * 时按键导致反复复位。
     * DOLPHIN_USB_STUCK_RESET_MS 仍作为绝对下界保留：即使宽限期提前结束，
     * 也要累计卡满这么久才允许动整颗芯片。
     *
     * 用 `watchdog_reboot()` 而不是 QMK 的 `mcu_reset()`（2026-08-11 改），两个理由：
     *
     * 1. **复位更彻底**。RP2040 上 `mcu_reset()` 就是 `NVIC_SystemReset()`
     *    （`platforms/chibios/bootloaders/rp2040.c:16`），只复位处理器子系统，**不复位外设**。
     *    而 `watchdog_reboot()` → `_watchdog_enable()` 会先设
     *      psm_hw->wdsel = PSM_WDSEL_BITS & ~(ROSC | XOSC)
     *    pico-sdk 原注释是「Reset everything apart from ROSC and XOSC」——
     *    **USB 外设块也在其中**。既然本故障首选根因是 E15 那种 USB 控制器硬件锁死，
     *    第 3 级理应用能真正复位 USB 块的那种复位。
     * 2. **给出可靠的「这次是软复位」信号**：`watchdog_hw->reason != 0`。
     *    诊断计数器靠它跨复位累计，见 dolphin_was_soft_reset()。
     *
     * delay_ms = 0 走 CTRL.TRIGGER 立即复位，所以这一行之后不会再执行任何代码。
     * 不会误进 UF2 模式：双击复位的 magic 在开机 500ms 后已被清零，而这里最早也是
     * 开机十几秒之后才可能触发。 */
    if (restart_tried && was_active && stuck_for > DOLPHIN_USB_STUCK_RESET_MS) {
        diag_reset_count_bump(); // 跨复位保留，复位后能知道这是第几次
        watchdog_reboot(0, 0, 0);
    }
}

void keyboard_post_init_kb(void) {
    /* 上电复位时 scratch[0] 是随机值，magic 校验虽有 1/65536 漏网率，仍显式清一次。
     * 判据是 `watchdog_hw->reason == 0`（理由见 dolphin_was_soft_reset() 上方注释）。
     * 必须在 watchdog_enable() 之前读 —— 虽然 `_watchdog_enable()` 并不写 REASON
     * （它是只读状态位、只由复位清除），但顺序摆对了更不容易出错。 */
    if (!dolphin_was_soft_reset()) {
        watchdog_hw->scratch[0] = 0;
    }

    // 在键盘完全初始化后才启用，避开启动阶段的 flash / USB 检测耗时
    watchdog_enable(DOLPHIN_WATCHDOG_TIMEOUT_MS, false);

    // keyboard_post_init_kb 的 weak 实现会调用 user 钩子，覆写后必须自己补上
    keyboard_post_init_user();
}

/* 把诊断计数敲出来。故意用 send_string 而不是 console：console 走的是同一条
 * 会被静默丢弃的 USB IN 通路，恰好在需要它的时候没输出。
 * 字段含义见 dolphin5x.h。 */
void dolphin_usb_diag_report(void) {
    char buf[80];
    snprintf(buf, sizeof(buf), "USBDIAG a=%u b=%u heal=%u rst=%u mcu=%u st=%u up=%lus", (unsigned)diag_cond_a, (unsigned)diag_cond_b, (unsigned)diag_selfhealed, (unsigned)diag_restarts, (unsigned)diag_reset_count(), (unsigned)diag_last_state, (unsigned long)(timer_read32() / 1000));
    send_string(buf);
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        last_key_activity = timer_read32();
    }

    // process_record_kb 的 weak 实现会调用 user 钩子，覆写后必须自己补上
    return process_record_user(keycode, record);
}

void housekeeping_task_kb(void) {
    // 喂狗。housekeeping 每轮主循环执行一次，卡死即停止喂狗。
    watchdog_update();

    usb_stuck_recovery_task();

    // 注意：housekeeping_task() 会分别调用 _modules / _kb / _user 三个钩子
    // （见 quantum/keyboard.c），这里不能再调用 housekeeping_task_user()，
    // 否则它每轮会被执行两次。
}

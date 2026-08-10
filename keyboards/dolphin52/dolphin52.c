// Copyright 2026 yekingyan
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "usb_main.h" // USB_DRIVER (== USBD1)
#include "hardware/watchdog.h"

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
 *   - 仍恢复不了则 `mcu_reset()` 兜底，等效于自动帮你拔插一次。
 *   - `was_active` 门控：只有「曾经正常工作过」才允许复位兜底，避免接充电头
 *     （有 5V 无主机）时按键导致反复复位。
 * ========================================================================== */

// 按键活动的有效窗口：距上次按下多久之内算「用户正在用」
#ifndef DOLPHIN_USB_ACTIVITY_WINDOW_MS
#    define DOLPHIN_USB_ACTIVITY_WINDOW_MS 3000
#endif
// 第 1 级：远程唤醒的重试节流间隔
#ifndef DOLPHIN_USB_WAKEUP_RETRY_MS
#    define DOLPHIN_USB_WAKEUP_RETRY_MS 250
#endif
// 第 2 级：卡这么久后重启 USB 驱动栈（官方 restart_usb_driver，不重启 MCU）
#ifndef DOLPHIN_USB_RESTART_AFTER_MS
#    define DOLPHIN_USB_RESTART_AFTER_MS 2000
#endif
// 第 2 级的重试节流（restart_usb_driver 内含 50ms 阻塞 + 主机重新枚举耗时）
#ifndef DOLPHIN_USB_RESTART_RETRY_MS
#    define DOLPHIN_USB_RESTART_RETRY_MS 3000
#endif
// 第 3 级：仍然救不回来就复位 MCU
#ifndef DOLPHIN_USB_STUCK_RESET_MS
#    define DOLPHIN_USB_STUCK_RESET_MS 10000
#endif

static uint32_t last_key_activity = 0;
static uint32_t stuck_since       = 0;     // 进入非 ACTIVE 的时刻，0 = 正常
static uint32_t last_wakeup_try   = 0;
static uint32_t last_restart_try  = 0;
static bool     was_active        = false; // 本次上电后是否曾经 USB_ACTIVE

/* USB GET_STATUS(Device) 的 Remote Wakeup Enabled 位（USB 2.0 规范 9.4.5，bit1）。
 * 上游把它定义在 tmk_core/protocol/chibios/chibios.c 内部而非头文件里，
 * 所以这里按同样的值重新定义一份。 */
#ifndef USB_GETSTATUS_REMOTE_WAKEUP_ENABLED
#    define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED (2U)
#endif

static void usb_stuck_recovery_task(void) {
    if (!is_keyboard_master()) {
        return; // 从手没有 USB 主机，这套逻辑不适用
    }

    if (USB_DRIVER.state == USB_ACTIVE) {
        was_active  = true;
        stuck_since = 0;
        return;
    }

    const uint32_t now = timer_read32();

    if (stuck_since == 0) {
        stuck_since = now ? now : 1; // 避开 0 这个「正常」哨兵值
    }

    // 用户没在按键就不动作：主机真休眠时保持安静。
    // last_key_activity == 0 表示本次上电以来还没按过任何键 —— 必须显式排除，
    // 否则 timer_elapsed32(0) 在开机头几秒内会小于窗口值，被误判成「刚有按键活动」。
    if (last_key_activity == 0 || timer_elapsed32(last_key_activity) > DOLPHIN_USB_ACTIVITY_WINDOW_MS) {
        return;
    }

    const uint32_t stuck_for = timer_elapsed32(stuck_since);

    /* 第 1 级：请求远程唤醒。最轻量，不打断任何东西。
     * usbWakeupHost() 内部自带 state == USB_SUSPENDED 判断，无条件调用是安全的；
     * 副作用是打开 DEV_SOF 中断，从而激活 LLD 中基于 SOF 的自愈路径。 */
    if (timer_elapsed32(last_wakeup_try) > DOLPHIN_USB_WAKEUP_RETRY_MS) {
        last_wakeup_try = now;
        if (USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED) {
            usbWakeupHost(&USB_DRIVER);
        }
    }

    /* 第 2 级：重启 USB 驱动栈。官方 API（tmk_core/protocol/chibios/usb_main.c:361，
     * 声明在 usb_main.h），做的是 usbDisconnectBus → usbStop → 停掉所有端点
     * → 等 50ms → 重新 init/start 所有端点 → usbStart → usbConnectBus。
     * 关键优点是**不重启 MCU**：层状态、粘滞修饰、分体链路、EEPROM 缓存全部保留，
     * 且能直接把卡住的 ChibiOS USB 状态机复位。代价是主机会重新枚举一次。 */
    if (stuck_for > DOLPHIN_USB_RESTART_AFTER_MS && timer_elapsed32(last_restart_try) > DOLPHIN_USB_RESTART_RETRY_MS) {
        last_restart_try = now;
        restart_usb_driver(&USB_DRIVER);
        return; // 给主机留出重新枚举的时间，下一轮再判断
    }

    /* 第 3 级：连驱动栈重启都救不回来（例如 RP2040 的 USB 控制器在硬件层面卡死），
     * 才复位 MCU。等效于自动帮你拔插一次。
     * was_active 门控：只有「曾经正常工作过」才允许，避免接充电头（有 5V 无主机）
     * 时按键导致反复复位。 */
    if (was_active && stuck_for > DOLPHIN_USB_STUCK_RESET_MS) {
        mcu_reset();
    }
}

void keyboard_post_init_kb(void) {
    // 在键盘完全初始化后才启用，避开启动阶段的 flash / USB 检测耗时
    watchdog_enable(DOLPHIN_WATCHDOG_TIMEOUT_MS, false);

    // keyboard_post_init_kb 的 weak 实现会调用 user 钩子，覆写后必须自己补上
    keyboard_post_init_user();
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

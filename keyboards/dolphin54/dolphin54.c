// Copyright 2026 yekingyan
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "hardware/watchdog.h"

/* RP2040 硬件看门狗
 *
 * 背景：QMK 的 `SPLIT_WATCHDOG_ENABLE` 只保护从手。见 vial-qmk
 * `quantum/split_common/split_util.c` 的 `split_watchdog_task()`：
 *     if (!split_watchdog_done && !is_keyboard_master()) { ... mcu_reset(); }
 * 主手一旦主循环卡死（键盘 HID 与 raw HID 同时失效，但 USB 因为由中断维持
 * 枚举，主机侧仍显示设备正常），就没有任何自恢复手段，只能手动拔插 USB。
 *
 * 这里启用 RP2040 的硬件看门狗，在 housekeeping（每轮主循环都会跑）里喂狗。
 * 主循环只要还在转就不会复位；一旦卡死，超时后芯片自动重启并重新枚举。
 *
 * 注意事项：
 * - `watchdog_enable()` 的上限约 8388ms（24 位计数器，每微秒递减 2）。
 * - 复位走 bootrom 的常规 flash 启动路径（`watchdog_enable` 写的
 *   scratch[4] magic 不是跳转 magic），不会误进 UF2 模式。
 * - 刷机用的 `bootloader_jump()` 走 bootrom 的 `reset_usb_boot()`，与看门狗
 *   互不干扰；双击复位的 magic 在开机 500ms 后已清零，而看门狗是在
 *   `keyboard_post_init_kb` 之后才启用的，不会误触发。
 * - 超时需留足余量给 EEPROM/flash 写入（Vial 改键、combo 默认值写入等）。
 */
#ifndef DOLPHIN_WATCHDOG_TIMEOUT_MS
#    define DOLPHIN_WATCHDOG_TIMEOUT_MS 4000
#endif

void keyboard_post_init_kb(void) {
    // 在键盘完全初始化后才启用，避开启动阶段的 flash / USB 检测耗时
    watchdog_enable(DOLPHIN_WATCHDOG_TIMEOUT_MS, false);

    // keyboard_post_init_kb 的 weak 实现会调用 user 钩子，覆写后必须自己补上
    keyboard_post_init_user();
}

void housekeeping_task_kb(void) {
    // 喂狗。housekeeping 每轮主循环执行一次，卡死即停止喂狗。
    watchdog_update();

    // 注意：housekeeping_task() 会分别调用 _modules / _kb / _user 三个钩子
    // （见 quantum/keyboard.c），这里不能再调用 housekeeping_task_user()，
    // 否则它每轮会被执行两次。
}

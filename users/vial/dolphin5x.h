// Copyright 2026 yekingyan
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* 把 USB 失声自恢复的诊断计数用 send_string 敲出来。
 *
 * 用法：在 keymap 里加一个自定义键码（见 keymaps/vial/keymap.c 的 USB_DIAG），
 * 按下时调用本函数，然后在任意文本框里看输出。
 *
 * 为什么不用 console：`dprintf` 走的是同一条会被静默丢弃的 USB IN 通路，
 * 恰好在需要它的时候没有输出（2026-08-10 实测：正常时能滚 log，失声后一行不出）。
 *
 * 输出格式（示例）：`USBDIAG a=3 b=12 heal=9 rst=4 mcu=1 st=4 up=1234s`
 *   a    条件(a) `state != USB_ACTIVE` 命中次数  → 软件状态机卡住
 *   b    条件(b) 帧号停滞命中次数                → E15 硬件锁死
 *   heal 检测到失活但在第 2 级触发前自愈的次数    → 这个数大说明检测器过敏
 *   rst  `restart_usb_driver()` 调用次数        → 与用户体感的卡顿次数对账
 *   mcu  第 3 级 `watchdog_reboot()` 次数（跨复位累计，存在 watchdog scratch[0]）
 *   st   最近一次判定失活时的 `USB_DRIVER.state`：2=USB_READY 4=USB_ACTIVE 5=USB_SUSPENDED
 *   up   本次上电已运行秒数
 */
void dolphin_usb_diag_report(void);

#pragma once

// Double-tap reset to enter bootloader
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 500U

// 解决无 VBUS 引脚的主从检测问题
#define SPLIT_USB_DETECT
#define SPLIT_USB_TIMEOUT 2500

// 锁定左手为主键盘（使用时 USB 请插左半区的主控）
#define MASTER_LEFT

// 从手看门狗：收不到主手通信就重启。注意它只作用于「被判定为从手」的那一侧，
// 主手的卡死保护由 dolphin52.c 里的 RP2040 硬件看门狗负责。
#define SPLIT_WATCHDOG_ENABLE

// 单半区可用性：主手在连续失败若干次后判定从手未连接，并对重试做节流。
// 这两个值就是 QMK 默认值，显式写出只是为了满足 SPLIT_WATCHDOG_ENABLE 的
// STATIC_ASSERT 并让配置自解释；它们不提供任何防死锁能力。
#define SPLIT_MAX_CONNECTION_ERRORS 10
#define SPLIT_CONNECTION_CHECK_TIMEOUT 500

// 防止笔记本 5V 持续供电导致的 USB 休眠唤醒假死（Wake-up Zombie State）
#define NO_SUSPEND_POWER_DOWN
#define NO_USB_STARTUP_CHECK



#pragma once

// Double-tap reset to enter bootloader
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 500U

// 解决无 VBUS 引脚的主从检测问题
#define SPLIT_USB_DETECT
#define SPLIT_USB_TIMEOUT 2500

// 锁定左手为主键盘（使用时 USB 请插左半区的主控）
#define MASTER_LEFT

// 看门狗，自动重启
#define SPLIT_WATCHDOG_ENABLE

// 防止笔记本 5V 持续供电导致的 USB 休眠唤醒假死（Wake-up Zombie State）
#define NO_SUSPEND_POWER_DOWN
#define NO_USB_STARTUP_CHECK


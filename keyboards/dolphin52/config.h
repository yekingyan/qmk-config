#pragma once

// Double-tap reset to enter bootloader
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 500U

// Split serial: half-duplex PIO on GP16 (single TRRS data wire)
#define SERIAL_USART_TX_PIN GP16

// Matrix: 10 rows x 6 cols (5 rows per half)
#define MATRIX_ROWS 10
#define MATRIX_COLS 6

#define DIRECT_PINS { \
    { GP3,  GP2,  GP1,  GP0,  GP29, GP28 }, \
    { GP7,  GP6,  GP5,  GP4,  GP27, GP26 }, \
    { GP11, GP10, GP9,  GP8,  GP22, GP21 }, \
    { GP14, GP15, GP13, GP12, GP19, GP20 }, \
    { GP17, GP18, NO_PIN, NO_PIN, NO_PIN, NO_PIN } \
}

// Both halves use identical PCB and pin mapping
#define DIRECT_PINS_RIGHT DIRECT_PINS

// 解决无 VBUS 引脚的主从检测问题
#define SPLIT_USB_DETECT
#define SPLIT_USB_TIMEOUT 2500

// 锁定左手为主键盘（使用时 USB 请插左半区的主控）
#define MASTER_LEFT

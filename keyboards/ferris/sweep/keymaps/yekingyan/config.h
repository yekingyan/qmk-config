/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

// ==========================================
// Vial 配置
// ==========================================
#define DYNAMIC_KEYMAP_LAYER_COUNT 7   // 7 层（Base/Nav/Num/Sym/Mouse/Fun/Media）
#define VIAL_TAP_DANCE_ENTRIES 10
#define VIAL_COMBO_ENTRIES 10          // 5 个 combo + 预留
#define VIAL_KEYBOARD_UID {0x59, 0x4B, 0x53, 0x57, 0x45, 0x45, 0x50, 0x37}

// 解锁 combo：左上角 Q (0,0) + 右上角 P (4,4) 同时按
#define VIAL_UNLOCK_COMBO_ROWS { 0, 4 }
#define VIAL_UNLOCK_COMBO_COLS { 0, 4 }

// ==========================================
// Tap-Hold 配置（模拟 ZMK balanced flavor）
// ==========================================
#define TAPPING_TERM 200
#define PERMISSIVE_HOLD
#define QUICK_TAP_TERM 150

// ==========================================
// One Shot Keys（Callum OSM）
// ==========================================
#define ONESHOT_TIMEOUT 1000

// ==========================================
// Combo
// ==========================================
#define COMBO_TERM 50
#define COMBO_MUST_TAP_PER_COMBO
#define COMBO_TERM_PER_COMBO

// ==========================================
// Caps Word
// ==========================================
#define CAPS_WORD_IDLE_TIMEOUT 5000

// ==========================================
// 鼠标键速度调优（模拟 ZMK mmv 配置）
// ==========================================
#define MOUSEKEY_DELAY 0
#define MOUSEKEY_INTERVAL 16
#define MOUSEKEY_MAX_SPEED 10
#define MOUSEKEY_TIME_TO_MAX 500
#define MOUSEKEY_WHEEL_DELAY 0
#define MOUSEKEY_WHEEL_INTERVAL 50
#define MOUSEKEY_WHEEL_MAX_SPEED 8
#define MOUSEKEY_WHEEL_TIME_TO_MAX 500

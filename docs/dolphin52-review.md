# Dolphin52 QMK 固件实现 — 专家审核文档

## 1. 项目背景

### 1.1 目标

为自制 52 键分体键盘 "Dolphin52" 编写 Vial QMK 固件，产出 `.uf2` 文件，通过 GitHub Actions 云编译。

### 1.2 前置经验

此前为 Ferris Sweep（34 键）成功移植了 ZMK → QMK 键位，但因 RP2040 Pro Micro 兼容主控的 GP26-29 ghost keys 硬件问题（ADC 复用引脚板载下拉电路导致始终读为低电平）而放弃该项目。Dolphin52 使用 YD-RP2040 主控，预期不存在此问题。

### 1.3 编译环境

- 基于 `vial-kb/vial-qmk` 仓库（vial 分支，29800+ commits，较新的 QMK fork）
- GitHub Actions 云编译：checkout vial-qmk → 复制键盘定义进去 → `make dolphin52:vial`
- 本地无 QMK CLI，无法本地验证编译

---

## 2. 硬件规格

### 2.1 主控

- **型号**：YD-RP2040
- **芯片**：RP2040（双核 Cortex-M0+，264KB SRAM，2MB Flash）
- **USB**：Type-C
- **与 Raspberry Pi Pico 的区别**：引脚排列不同，无板载 VBUS_SENSE 电路

### 2.2 键盘物理布局

- **总键数**：52 键（左右各 26 键）
- **布局**：4 行 × 6 列 + 2 拇指键（每侧）
- **连接方式**：Direct Pin（每键独占一个 GPIO，非矩阵扫描）
- **触发逻辑**：Active Low + Internal Pull-Up（按键一端接 GPIO，另一端接 GND）
- **分体通信**：TRRS 线缆，单根数据线（半双工），使用 GP16

### 2.3 引脚映射（左半区，右半区 PCB 完全相同）

```
行0 (数字行): GP3  GP2  GP1  GP0  GP29 GP28
行1 (Q行):    GP7  GP6  GP5  GP4  GP27 GP26
行2 (A行):    GP11 GP10 GP9  GP8  GP22 GP21
行3 (Z行):    GP14 GP15 GP13 GP12 GP19 GP20
行4 (拇指):   GP17 GP18 —    —    —    —
分体通信:     GP16
```

### 2.4 ADC 引脚风险

GP26、GP27、GP28、GP29 均用作按键输入。在之前的 Pro Micro 兼容主控上这些引脚出现 ghost keys，但 YD-RP2040 预期无此问题（无板载 ADC 分压电路）。**这是一个需要实际验证的风险点。**

---

## 3. 软件架构决策

### 3.1 为什么用 keyboard.json + config.h 混合方式

- `keyboard.json`：定义键盘元信息（名称、USB ID、processor、bootloader）和物理布局（layout）
- `config.h`：定义矩阵引脚（DIRECT_PINS）、串口通信引脚、RP2040 特定配置

**原因**：vial-qmk 对纯 data-driven（全 JSON）的支持程度不确定，特别是 split serial 配置。参考了 Piantor 键盘（同为 RP2040 + direct pin + split + vial）的做法，采用 config.h 定义矩阵和串口。

### 3.2 Split 通信方案

- **驱动**：`SERIAL_DRIVER = vendor`（在 RP2040 上映射到 PIO 驱动）
- **模式**：半双工（单根 TRRS 数据线）
- **引脚**：`SERIAL_USART_TX_PIN = GP16`
- **Board**：`BOARD = GENERIC_RP_RP2040`（非 Pro Micro 兼容板，不使用预定义引脚映射）

### 3.3 键位设计

- **核心区**（每侧 17 键 = 3×5 + 2 拇指）：完整保留 Sweep 7 层
- **外围区**（每侧 9 键 = 数字行 6 + 修饰列 3）：传统键位
- **7 层**：Base / Nav / Num / Sym / Mouse / Fun / Media
- **特殊功能**：Swapper（Alt-Tab 宏）、Caps Word、Callum OSM

---

## 4. 需要审核的具体问题

### 4.1 config.h 中的矩阵定义是否正确？

```c
#define MATRIX_ROWS 10
#define MATRIX_COLS 6

#define DIRECT_PINS { \
    { GP3,  GP2,  GP1,  GP0,  GP29, GP28 }, \
    { GP7,  GP6,  GP5,  GP4,  GP27, GP26 }, \
    { GP11, GP10, GP9,  GP8,  GP22, GP21 }, \
    { GP14, GP15, GP13, GP12, GP19, GP20 }, \
    { GP17, GP18, NO_PIN, NO_PIN, NO_PIN, NO_PIN } \
}

#define DIRECT_PINS_RIGHT DIRECT_PINS
```

**疑问**：
1. `MATRIX_ROWS = 10` 是否正确？（5 行/半 × 2 半 = 10）
2. `DIRECT_PINS` 只定义一半的 5 行，QMK split 框架会自动将其映射到 rows 0-4（左）和 rows 5-9（右）？
3. `DIRECT_PINS_RIGHT DIRECT_PINS` 表示两半 PCB 引脚完全相同，这样写对吗？

### 4.2 Split serial 配置是否正确？

```c
#define SERIAL_USART_TX_PIN GP16
```

```makefile
SERIAL_DRIVER = vendor
```

**疑问**：
1. 半双工 PIO 只需要定义 TX_PIN 吗？是否还需要 `#define SERIAL_USART_HALF_DUPLEX`？
2. `vendor` driver 在 RP2040 上是否默认使用 PIO？
3. GP16 是否适合做 PIO serial？（是否有 PIO 功能限制？）

### 4.3 keyboard.json 与 config.h 是否冲突？

`keyboard.json` 中有 `"split": {"enabled": true}` 但没有 `matrix_pins`。`config.h` 中定义了 `DIRECT_PINS` 和 `MATRIX_ROWS/COLS`。

**疑问**：这种混合方式在 vial-qmk 中是否会产生冲突或被忽略？

### 4.4 GENERIC_RP_RP2040 board 是否合适？

YD-RP2040 不是 Pro Micro 兼容板，选择 `BOARD = GENERIC_RP_RP2040` 意味着不使用任何预定义引脚。

**疑问**：是否还需要额外配置（如 flash 芯片类型、VBUS 检测）？

### 4.5 Vial 解锁 combo 的矩阵位置

```c
#define VIAL_UNLOCK_COMBO_ROWS { 0, 5 }
#define VIAL_UNLOCK_COMBO_COLS { 0, 5 }
```

这表示左半 [0,0]（左上角 `~` 键）和右半 [5,5]（右上角 `-` 键）同时按下解锁。

**疑问**：右半的矩阵位置 [5,5] 是否正确？（右半第一行最后一列）

---

## 5. 完整源码

以下是所有文件的完整内容。

### 5.1 `keyboards/dolphin52/keyboard.json`

```json
{
    "manufacturer": "YeKingYan",
    "keyboard_name": "Dolphin52",
    "maintainer": "yekingyan",
    "bootloader": "rp2040",
    "features": {
        "bootmagic": true,
        "extrakey": true,
        "mousekey": true
    },
    "processor": "RP2040",
    "split": {
        "enabled": true
    },
    "url": "",
    "usb": {
        "device_version": "0.0.1",
        "pid": "0xD052",
        "vid": "0x594B"
    },
    "layouts": {
        "LAYOUT": {
            "layout": [
                {"matrix": [0, 0], "x": 0,  "y": 0},
                {"matrix": [0, 1], "x": 1,  "y": 0},
                {"matrix": [0, 2], "x": 2,  "y": 0},
                {"matrix": [0, 3], "x": 3,  "y": 0},
                {"matrix": [0, 4], "x": 4,  "y": 0},
                {"matrix": [0, 5], "x": 5,  "y": 0},
                {"matrix": [5, 0], "x": 9,  "y": 0},
                {"matrix": [5, 1], "x": 10, "y": 0},
                {"matrix": [5, 2], "x": 11, "y": 0},
                {"matrix": [5, 3], "x": 12, "y": 0},
                {"matrix": [5, 4], "x": 13, "y": 0},
                {"matrix": [5, 5], "x": 14, "y": 0},
                {"matrix": [1, 0], "x": 0,  "y": 1},
                {"matrix": [1, 1], "x": 1,  "y": 1},
                {"matrix": [1, 2], "x": 2,  "y": 1},
                {"matrix": [1, 3], "x": 3,  "y": 1},
                {"matrix": [1, 4], "x": 4,  "y": 1},
                {"matrix": [1, 5], "x": 5,  "y": 1},
                {"matrix": [6, 0], "x": 9,  "y": 1},
                {"matrix": [6, 1], "x": 10, "y": 1},
                {"matrix": [6, 2], "x": 11, "y": 1},
                {"matrix": [6, 3], "x": 12, "y": 1},
                {"matrix": [6, 4], "x": 13, "y": 1},
                {"matrix": [6, 5], "x": 14, "y": 1},
                {"matrix": [2, 0], "x": 0,  "y": 2},
                {"matrix": [2, 1], "x": 1,  "y": 2},
                {"matrix": [2, 2], "x": 2,  "y": 2},
                {"matrix": [2, 3], "x": 3,  "y": 2},
                {"matrix": [2, 4], "x": 4,  "y": 2},
                {"matrix": [2, 5], "x": 5,  "y": 2},
                {"matrix": [7, 0], "x": 9,  "y": 2},
                {"matrix": [7, 1], "x": 10, "y": 2},
                {"matrix": [7, 2], "x": 11, "y": 2},
                {"matrix": [7, 3], "x": 12, "y": 2},
                {"matrix": [7, 4], "x": 13, "y": 2},
                {"matrix": [7, 5], "x": 14, "y": 2},
                {"matrix": [3, 0], "x": 0,  "y": 3},
                {"matrix": [3, 1], "x": 1,  "y": 3},
                {"matrix": [3, 2], "x": 2,  "y": 3},
                {"matrix": [3, 3], "x": 3,  "y": 3},
                {"matrix": [3, 4], "x": 4,  "y": 3},
                {"matrix": [3, 5], "x": 5,  "y": 3},
                {"matrix": [8, 0], "x": 9,  "y": 3},
                {"matrix": [8, 1], "x": 10, "y": 3},
                {"matrix": [8, 2], "x": 11, "y": 3},
                {"matrix": [8, 3], "x": 12, "y": 3},
                {"matrix": [8, 4], "x": 13, "y": 3},
                {"matrix": [8, 5], "x": 14, "y": 3},
                {"matrix": [4, 0], "x": 4,  "y": 4},
                {"matrix": [4, 1], "x": 5,  "y": 4},
                {"matrix": [9, 0], "x": 9,  "y": 4},
                {"matrix": [9, 1], "x": 10, "y": 4}
            ]
        }
    }
}
```

### 5.2 `keyboards/dolphin52/config.h`

```c
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
```

### 5.3 `keyboards/dolphin52/rules.mk`

```makefile
BOARD = GENERIC_RP_RP2040
SERIAL_DRIVER = vendor
```

### 5.4 `keyboards/dolphin52/keymaps/vial/config.h`

```c
#pragma once

// Vial
#define DYNAMIC_KEYMAP_LAYER_COUNT 7
#define VIAL_TAP_DANCE_ENTRIES 10
#define VIAL_COMBO_ENTRIES 10
#define VIAL_KEYBOARD_UID {0x44, 0x4F, 0x4C, 0x50, 0x48, 0x49, 0x4E, 0x35}

// Vial 解锁 combo: 左上角 [0,0] + 右上角 [5,5]
#define VIAL_UNLOCK_COMBO_ROWS { 0, 5 }
#define VIAL_UNLOCK_COMBO_COLS { 0, 5 }

// Tap-Hold
#define TAPPING_TERM 200
#define PERMISSIVE_HOLD
#define QUICK_TAP_TERM 150

// One Shot
#define ONESHOT_TIMEOUT 1000

// Combo
#define COMBO_TERM 50

// Caps Word
#define CAPS_WORD_IDLE_TIMEOUT 5000

// Mouse
#define MOUSEKEY_DELAY 0
#define MOUSEKEY_INTERVAL 16
#define MOUSEKEY_MAX_SPEED 10
#define MOUSEKEY_TIME_TO_MAX 40
#define MOUSEKEY_WHEEL_DELAY 0
#define MOUSEKEY_WHEEL_INTERVAL 50
#define MOUSEKEY_WHEEL_MAX_SPEED 8
#define MOUSEKEY_WHEEL_TIME_TO_MAX 40
```

### 5.5 `keyboards/dolphin52/keymaps/vial/rules.mk`

```makefile
VIA_ENABLE = yes
VIAL_ENABLE = yes
COMBO_ENABLE = yes
CAPS_WORD_ENABLE = yes
LTO_ENABLE = yes
```

### 5.6 `keyboards/dolphin52/keymaps/vial/vial.json`

```json
{
  "name": "Dolphin52",
  "vendorId": "0x594B",
  "productId": "0xD052",
  "firmwareVersion": 0,
  "lighting": "none",
  "matrix": {
    "rows": 10,
    "cols": 6
  },
  "layouts": {
    "keymap": [
      ["0,0","0,1","0,2","0,3","0,4","0,5",{"x":1},"5,0","5,1","5,2","5,3","5,4","5,5"],
      ["1,0","1,1","1,2","1,3","1,4","1,5",{"x":1},"6,0","6,1","6,2","6,3","6,4","6,5"],
      ["2,0","2,1","2,2","2,3","2,4","2,5",{"x":1},"7,0","7,1","7,2","7,3","7,4","7,5"],
      ["3,0","3,1","3,2","3,3","3,4","3,5",{"x":1},"8,0","8,1","8,2","8,3","8,4","8,5"],
      [{"x":4},"4,0","4,1",{"x":1},"9,0","9,1"]
    ]
  }
}
```

### 5.7 `keyboards/dolphin52/keymaps/vial/keymap.c`

```c
/*
 * Dolphin52 — 52 键分体键盘 QMK 布局
 * 核心：Sweep 7 层 (Base/Nav/Num/Sym/Mouse/Fun/Media)
 * 外围：传统键位 (数字行 + 修饰列)
 *
 * 布局：4x6 + 2 拇指 (每侧 26 键)
 * 核心区 = 内侧 3x5 + 2 拇指 (与 Sweep 完全一致)
 * 外围区 = 数字行 6 键 + 修饰列 3 键
 */

#include QMK_KEYBOARD_H

enum layers {
    _BASE,
    _NAV,
    _NUM,
    _SYM,
    _MOUSE,
    _FUN,
    _MEDIA,
};

enum custom_keycodes {
    SW_APP = SAFE_RANGE,
};

static bool sw_app_active = false;

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case SW_APP:
            if (record->event.pressed) {
                if (!sw_app_active) {
                    sw_app_active = true;
                    register_code(KC_LALT);
                }
                tap_code(KC_TAB);
            }
            return false;
    }
    return true;
}

layer_state_t layer_state_set_user(layer_state_t state) {
    if (sw_app_active && !layer_state_cmp(state, _NAV)) {
        unregister_code(KC_LALT);
        sw_app_active = false;
    }
    return state;
}

bool caps_word_press_user(uint16_t keycode) {
    switch (keycode) {
        case KC_A ... KC_Z:
        case KC_1 ... KC_0:
            add_weak_mods(MOD_BIT(KC_LSFT));
            return true;
        case KC_MINS:
        case KC_UNDS:
        case KC_BSPC:
        case KC_DEL:
            return true;
        default:
            return false;
    }
}

/* 52 键 LAYOUT 宏排列:
 * 左数字行(6) 右数字行(6)
 * 左Q行(6)    右Q行(6)
 * 左A行(6)    右A行(6)
 * 左Z行(6)    右Z行(6)
 * 左拇指(2)   右拇指(2)
 */

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [_BASE] = LAYOUT(
        // 数字行 (外围)
        KC_GRV,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,       KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS,
        // Q 行: 外围Tab + 核心5 | 核心5 + 外围'
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,       KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_QUOT,
        // A 行: 外围LShift + 核心5 | 核心5 + 外围RShift
        KC_LSFT, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,       KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_RSFT,
        // Z 行: 外围LCtrl + 核心5 | 核心5 + 外围Backslash
        KC_LCTL, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,       KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_BSLS,
        // 拇指 (核心)
        LT(_NAV, KC_SPC), LT(_NUM, KC_TAB),                      LT(_SYM, KC_ENT), LT(_MOUSE, KC_BSPC)
    ),

    [_NAV] = LAYOUT(
        KC_ESC,  KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,      KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,
        _______, SW_APP,  S(KC_TAB),KC_ENT,  KC_LSFT, KC_BSPC,    C(KC_LEFT),C(KC_D),C(KC_U),C(KC_RGHT),KC_DEL,KC_F12,
        _______, OSM(MOD_LGUI),OSM(MOD_LALT),OSM(MOD_LCTL),OSM(MOD_LSFT),CW_TOGG, KC_LEFT,KC_DOWN,KC_UP,KC_RGHT,C(KC_DEL),_______,
        _______, C(KC_Z), C(KC_X), C(KC_C), C(KC_V), _______,    KC_HOME, KC_PGDN, KC_PGUP, KC_END,  C(KC_BSPC),_______,
        _______, _______,                                          _______, _______
    ),

    [_NUM] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,    KC_DOT,  KC_7,    KC_8,    KC_9,    KC_MINS, _______,
        _______, OSM(MOD_LGUI),OSM(MOD_LALT),OSM(MOD_LCTL),OSM(MOD_LSFT),KC_BSPC, _______,KC_4,KC_5,KC_6,KC_PLUS,_______,
        _______, _______, KC_SLSH, KC_ASTR, KC_EQL,  _______,    _______, KC_1,    KC_2,    KC_3,    _______, _______,
        _______, _______,                                          _______, KC_0
    ),

    [_SYM] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, KC_EXLM, KC_AT,   KC_HASH, KC_DLR,  KC_PERC,    KC_CIRC, KC_AMPR, KC_ASTR, KC_EQL,  KC_QUOT, _______,
        _______, KC_LPRN, KC_LCBR, KC_LBRC, KC_LT,   KC_UNDS,    KC_PIPE, OSM(MOD_LSFT),OSM(MOD_RCTL),OSM(MOD_RALT),OSM(MOD_RGUI),_______,
        _______, KC_RPRN, KC_RCBR, KC_RBRC, KC_GT,   KC_GRV,     KC_BSLS, KC_TILD, KC_COLN, KC_DQUO, KC_QUES, _______,
        _______, _______,                                          _______, _______
    ),

    [_MOUSE] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, C(KC_Z), C(KC_Y), C(G(KC_LEFT)),C(G(KC_RGHT)),_______, _______, _______, _______, _______, _______, _______,
        _______, _______, KC_MS_L, KC_MS_U, KC_MS_D, KC_MS_R,    _______, OSM(MOD_LSFT),OSM(MOD_RCTL),OSM(MOD_RALT),OSM(MOD_RGUI),_______,
        _______, _______, KC_WH_L, KC_WH_U, KC_WH_D, KC_WH_R,    _______, _______, _______, _______, _______, _______,
        KC_BTN2, KC_BTN1,                                          _______, _______
    ),

    [_FUN] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, KC_CANCEL,_______,_______,  _______, _______,    _______, KC_F7,   KC_F8,   KC_F9,   KC_F12,  _______,
        _______, OSM(MOD_LGUI),OSM(MOD_LALT),OSM(MOD_LCTL),OSM(MOD_LSFT),_______, _______,KC_F4,KC_F5,KC_F6,KC_F11,_______,
        _______, _______, _______, _______, _______, _______,    _______, KC_F1,   KC_F2,   KC_F3,   KC_F10,  _______,
        _______, _______,                                          _______, _______
    ),

    [_MEDIA] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, _______, KC_MPRV, KC_VOLD, KC_VOLU, KC_MNXT,    _______, OSM(MOD_LSFT),OSM(MOD_RCTL),OSM(MOD_RALT),OSM(MOD_RGUI),_______,
        _______, _______, KC_MUTE, KC_BRID, KC_BRIU, KC_MPLY,    _______, _______, _______, _______, _______, _______,
        _______, _______,                                          _______, _______
    ),
};
```

### 5.8 `.github/workflows/build_binaries.yaml`

```yaml
name: Build Vial firmware

on: [push, workflow_dispatch]

permissions:
  contents: write

jobs:
  build:
    runs-on: ubuntu-latest

    steps:
      - name: Checkout userspace
        uses: actions/checkout@v4

      - name: Checkout vial-qmk
        uses: actions/checkout@v4
        with:
          repository: vial-kb/vial-qmk
          ref: vial
          path: vial-qmk
          submodules: recursive

      - name: Copy keyboard definitions into vial-qmk
        run: |
          cp -r keyboards/dolphin52 vial-qmk/keyboards/dolphin52

      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Install QMK CLI
        run: |
          python -m pip install --upgrade pip
          pip install qmk

      - name: QMK setup
        run: |
          cd vial-qmk
          qmk setup --yes --home .

      - name: Build Dolphin52 firmware
        run: |
          cd vial-qmk
          make dolphin52:vial

      - name: Upload firmware
        uses: actions/upload-artifact@v4
        with:
          name: dolphin52-vial-firmware
          path: |
            vial-qmk/*.uf2
```

---

## 6. 参考项目

| 项目 | 相似点 | 参考了什么 |
|------|--------|-----------|
| Ferris Sweep (vial-qmk 内置) | 同为 split + direct pin，使用 keyboard.json | 文件格式、layout 定义方式 |
| beekeeb Piantor | RP2040 + direct pin + split + vial | config.h 中 DIRECT_PINS 写法、SERIAL_USART 配置、GENERIC_RP_RP2040 board |
| 本项目 Ferris Sweep keymap | 同作者的 Sweep 键位 | 7 层 keymap 逻辑、Swapper、Caps Word |

---

## 7. 已知风险和待验证事项

1. **GP26-29 在 YD-RP2040 上是否正常工作**：之前在 Pro Micro 兼容板上失败，YD-RP2040 理论上无此问题但未实测
2. **vial-qmk 对 keyboard.json + config.h 混合模式的兼容性**：未本地编译验证
3. **半双工 PIO serial 是否需要额外配置**：`SERIAL_USART_HALF_DUPLEX` 是否必须显式定义？
4. **SPLIT_USB_DETECT**：YD-RP2040 无 VBUS_SENSE 电路，ChibiOS 默认使用 SPLIT_USB_DETECT，是否有延迟/可靠性问题？
5. **FUN 和 MEDIA 层的触发方式**：当前 keymap 中没有直接触发 FUN/MEDIA 层的键，需要后续在 Vial 中配置 combo（左双拇指 → FUN，右双拇指 → MEDIA）

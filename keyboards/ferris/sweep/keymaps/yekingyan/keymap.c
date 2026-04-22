/*
 * Ferris Sweep (cradio) 34 键 QMK 布局
 * 从 ZMK "双核驱动" 移植，保留 Callum OSM + Vim Nav 核心
 *
 * 布局：3x5+2 (每侧 17 键)
 * 拇指键：SPACE(NAV) TAB(NUM) | ENTER(SYM) BSPC(MOUSE)
 *   FUN = 左双拇指 combo  |  MEDIA = 右双拇指 combo
 *
 * Layer 0: Base     - QWERTY
 * Layer 1: Nav      - 左手 OSM + 右手 Vim HJKL
 * Layer 2: Numpad   - 左手算术运算 + 右手纯数字九宫格
 * Layer 3: Symbols  - 左手符号区 + 右手 OSM 修饰键
 * Layer 4: Mouse    - 左手鼠标移动/滚轮/点击 + 右手 OSM
 * Layer 5: Function - 左手 OSM + 右手 F 键九宫格
 * Layer 6: Media    - 左手媒体/音量/亮度 + 右手 OSM
 *
 * SPDX-License-Identifier: MIT
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

// ==========================================
// 自定义 Keycode：Swapper (Alt-Tab 宏)
// ==========================================
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

// Nav 层退出时释放 Alt（swapper 清理）
layer_state_t layer_state_set_user(layer_state_t state) {
    if (sw_app_active && !layer_state_cmp(state, _NAV)) {
        unregister_code(KC_LALT);
        sw_app_active = false;
    }
    return state;
}

// ==========================================
// Caps Word：下划线和连字符不中断
// ==========================================
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

// ==========================================
// Combos
// ==========================================
// Cradio 键位编号 (34 键):
//  0  1  2  3  4       5  6  7  8  9
// 10 11 12 13 14      15 16 17 18 19
// 20 21 22 23 24      25 26 27 28 29
//             30 31  32 33

enum combos {
    COMBO_ESC,
    COMBO_CAPS_WORD,
    COMBO_LSHFT,
    COMBO_FUN,
    COMBO_MEDIA,
    COMBO_LENGTH
};
uint16_t COMBO_LEN = COMBO_LENGTH;

const uint16_t PROGMEM combo_esc[]       = {KC_S, KC_D, COMBO_END};
const uint16_t PROGMEM combo_caps_word[] = {KC_F, KC_J, COMBO_END};
const uint16_t PROGMEM combo_lshft[]     = {KC_J, KC_K, COMBO_END};
const uint16_t PROGMEM combo_fun[]       = {LT(_NAV, KC_SPC), LT(_NUM, KC_TAB), COMBO_END};
const uint16_t PROGMEM combo_media[]     = {LT(_SYM, KC_ENT), LT(_MOUSE, KC_BSPC), COMBO_END};

combo_t key_combos[] = {
    [COMBO_ESC]       = COMBO(combo_esc, KC_ESC),
    [COMBO_CAPS_WORD] = COMBO(combo_caps_word, CW_TOGG),
    [COMBO_LSHFT]     = COMBO(combo_lshft, KC_LSFT),
    [COMBO_FUN]       = COMBO(combo_fun, MO(_FUN)),
    [COMBO_MEDIA]     = COMBO(combo_media, MO(_MEDIA)),
};

bool get_combo_must_tap(uint16_t combo_index, combo_t *combo) {
    switch (combo_index) {
        case COMBO_ESC:
        case COMBO_CAPS_WORD:
        case COMBO_LSHFT:
            return true;
        default:
            return false;
    }
}

// ==========================================
// Keymap: 7 层
// ==========================================
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    // Layer 0: Base (QWERTY)
    [_BASE] = LAYOUT_split_3x5_2(
        KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,             KC_Y,    KC_U,    KC_I,     KC_O,    KC_P,
        KC_A,    KC_S,    KC_D,    KC_F,    KC_G,             KC_H,    KC_J,    KC_K,     KC_L,    KC_SCLN,
        KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,             KC_N,    KC_M,    KC_COMM,  KC_DOT,  KC_SLSH,
                          LT(_NAV, KC_SPC), LT(_NUM, KC_TAB), LT(_SYM, KC_ENT), LT(_MOUSE, KC_BSPC)
    ),

    // Layer 1: Nav & Mods
    [_NAV] = LAYOUT_split_3x5_2(
        SW_APP,        S(KC_TAB),     KC_ENT,        KC_LSFT,        KC_BSPC,           C(KC_LEFT), C(KC_D),    C(KC_U),    C(KC_RGHT), KC_DEL,
        OSM(MOD_LGUI), OSM(MOD_LALT), OSM(MOD_LCTL), OSM(MOD_LSFT),  CW_TOGG,           KC_LEFT,    KC_DOWN,    KC_UP,      KC_RGHT,    C(KC_DEL),
        C(KC_Z),       C(KC_X),       C(KC_C),       C(KC_V),        _______,           KC_HOME,    KC_PGDN,    KC_PGUP,    KC_END,     C(KC_BSPC),
                                                      _______,        _______,           _______,    _______
    ),

    // Layer 2: Numpad
    [_NUM] = LAYOUT_split_3x5_2(
        _______,        _______,        _______,        _______,        _______,           KC_DOT,  KC_7,    KC_8,    KC_9,    KC_MINS,
        OSM(MOD_LGUI),  OSM(MOD_LALT),  OSM(MOD_LCTL),  OSM(MOD_LSFT),  KC_BSPC,           _______, KC_4,    KC_5,    KC_6,    KC_PLUS,
        _______,        KC_SLSH,        KC_ASTR,        KC_EQL,         _______,           _______, KC_1,    KC_2,    KC_3,    _______,
                                                         _______,        _______,           _______, KC_0
    ),

    // Layer 3: Symbols
    [_SYM] = LAYOUT_split_3x5_2(
        KC_EXLM, KC_AT,   KC_HASH, KC_DLR,  KC_PERC,          KC_CIRC, KC_AMPR,        KC_ASTR,        KC_EQL,         KC_QUOT,
        KC_LPRN, KC_LCBR, KC_LBRC, KC_LT,   KC_UNDS,          KC_PIPE, OSM(MOD_LSFT),  OSM(MOD_RCTL),  OSM(MOD_RALT),  OSM(MOD_RGUI),
        KC_RPRN, KC_RCBR, KC_RBRC, KC_GT,   KC_GRV,           KC_BSLS, KC_TILD,        KC_COLN,        KC_DQUO,        KC_QUES,
                                    _______, _______,           _______, _______
    ),

    // Layer 4: Mouse
    [_MOUSE] = LAYOUT_split_3x5_2(
        C(KC_Z),  C(KC_Y),       C(G(KC_LEFT)), C(G(KC_RGHT)), _______,           _______, _______,        _______,        _______,        _______,
        _______,  KC_MS_L,       KC_MS_U,       KC_MS_D,        KC_MS_R,           _______, OSM(MOD_LSFT),  OSM(MOD_RCTL),  OSM(MOD_RALT),  OSM(MOD_RGUI),
        _______,  KC_WH_L,       KC_WH_U,       KC_WH_D,        KC_WH_R,           _______, _______,        _______,        _______,        _______,
                                                 KC_BTN2,        KC_BTN1,           _______, _______
    ),

    // Layer 5: Function（BLE/BT 已移除，QMK 有线不需要）
    [_FUN] = LAYOUT_split_3x5_2(
        KC_CANCEL,      _______,        _______,        _______,        _______,           _______, KC_F7,   KC_F8,   KC_F9,   KC_F12,
        OSM(MOD_LGUI),  OSM(MOD_LALT),  OSM(MOD_LCTL),  OSM(MOD_LSFT),  _______,           _______, KC_F4,   KC_F5,   KC_F6,   KC_F11,
        _______,        _______,        _______,        _______,        _______,           _______, KC_F1,   KC_F2,   KC_F3,   KC_F10,
                                                         _______,        _______,           _______, _______
    ),

    // Layer 6: Media
    [_MEDIA] = LAYOUT_split_3x5_2(
        _______,  _______,  _______,  _______,  _______,          _______,  _______,        _______,        _______,        _______,
        _______,  KC_MPRV,  KC_VOLD,  KC_VOLU,  KC_MNXT,          _______,  OSM(MOD_LSFT),  OSM(MOD_RCTL),  OSM(MOD_RALT),  OSM(MOD_RGUI),
        _______,  KC_MUTE,  KC_BRID,  KC_BRIU,  KC_MPLY,          _______,  _______,        _______,        _______,        _______,
                                       _______,  _______,          _______,  _______
    ),
};

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
    _NAV_MAC,  // Nav 的 Mac 版 (⌘ 剪贴板, ⌥ 词跳)
};

enum custom_keycodes {
    SW_APP = QK_KB_0,
    SK_LGUI,  // Sticky Gui  (粘滞 Win)
    SK_LALT,  // Sticky Alt
    SK_LCTL,  // Sticky Ctrl
    SK_LSFT,  // Sticky Shift
    // Mac Nav: ⌘ 剪贴板 + ⌥ 方向键
    SW_APP_MAC,  // ⌘+Tab (Mac 版 Swapper)
};

static bool sw_app_active = false;
static bool sw_app_mac = false;  // true=⌘+Tab, false=Alt+Tab

// 自定义粘滞修饰 (替代内置 OSM，规避 LT 层干扰)
// 支持 chain 模式: 多个修饰键累加，非修饰键松开时全部释放
// 用 register_code(KC_*) 而非 register_mods()，确保 IME 能识别 LSHFT 切换中英文
static uint8_t  sticky_mods = 0;
static uint16_t sticky_deadline = 0;

#define STICKY_TIMEOUT_MS 1000  // 1s 超时自动释放

static uint8_t sticky_mod_to_kc(uint8_t mod) {
    if (mod & MOD_BIT(KC_LSFT)) return KC_LSFT;
    if (mod & MOD_BIT(KC_LCTL)) return KC_LCTL;
    if (mod & MOD_BIT(KC_LALT)) return KC_LALT;
    if (mod & MOD_BIT(KC_LGUI)) return KC_LGUI;
    return KC_NO;
}

static void sticky_mod_add(uint8_t mod) {
    sticky_mods |= mod;
    register_code(sticky_mod_to_kc(mod));
    sticky_deadline = timer_read() + STICKY_TIMEOUT_MS;
}

static void sticky_mod_clear(void) {
    if (sticky_mods) {
        for (uint8_t m = sticky_mods; m; m &= (m - 1)) {
            unregister_code(sticky_mod_to_kc(m & -m));
        }
        sticky_mods = 0;
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case SW_APP:
            if (record->event.pressed) {
                if (!sw_app_active) {
                    sw_app_active = true;
                    sw_app_mac = false;
                    register_code(KC_LALT);
                }
                tap_code(KC_TAB);
            }
            return false;

        case SK_LGUI: case SK_LALT: case SK_LCTL: case SK_LSFT:
            if (record->event.pressed) {
                uint8_t mod = (keycode == SK_LGUI) ? MOD_BIT(KC_LGUI) :
                             (keycode == SK_LALT) ? MOD_BIT(KC_LALT) :
                             (keycode == SK_LCTL) ? MOD_BIT(KC_LCTL) :
                                                    MOD_BIT(KC_LSFT);
                sticky_mod_add(mod);  // 累加，重复按等同刷新计时
            }
            return false;

        case SW_APP_MAC:
            if (record->event.pressed) {
                if (!sw_app_active) {
                    sw_app_active = true;
                    sw_app_mac = true;
                    register_code(KC_LGUI);
                }
                tap_code(KC_TAB);
            }
            return false;
    }

    // 非修饰键松开时释放所有粘滞修饰 (chain 消费)
    // 排除: 所有自定义键码 (管理自己的修饰键)、LT/MO 层切换键
    if (sticky_mods
        && (keycode < QK_KB || keycode > QK_KB_MAX)
        && !IS_QK_LAYER_TAP(keycode)
        && !IS_QK_MOMENTARY(keycode)) {
        if (!record->event.pressed) {
            sticky_mod_clear();
        }
    }
    return true;
}

void matrix_scan_user(void) {
    // 粘滞修饰超时自动释放 (产生独立修饰键输出，如切换输入法)
    if (sticky_mods && timer_expired(timer_read(), sticky_deadline)) {
        sticky_mod_clear();
    }
}

layer_state_t layer_state_set_user(layer_state_t state) {
    // Swapper 离开 Nav 层自动释放修饰键
    if (sw_app_active && !layer_state_cmp(state, _NAV) && !layer_state_cmp(state, _NAV_MAC)) {
        unregister_code(sw_app_mac ? KC_LGUI : KC_LALT);
        sw_app_active = false;
    }
    // 双拇指切层: 左 Space+Tab -> Fun, 右 Enter+Bspc -> Media
    state = update_tri_layer_state(state, _NAV, _NUM, _FUN);
    state = update_tri_layer_state(state, _SYM, _MOUSE, _MEDIA);
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
        KC_ESC,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,       KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_DEL,
        // Q 行: 外围~ + 核心5 | 核心5 + 外围[
        KC_GRV,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,       KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC,
        // A 行: 外围LShift + 核心5 | 核心5 + 外围'
        KC_LSFT, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,       KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,
        // Z 行: 外围LCtrl + 核心5 | 核心5 + 外围RAlt
        KC_LCTL, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,       KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RALT,
        // 拇指 (核心) + GP23 测试键
        LT(_NAV, KC_SPC), LT(_NUM, KC_TAB), KC_9,                LT(_SYM, KC_ENT), LT(_MOUSE, KC_BSPC)
    ),

    [_NAV] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, SW_APP,  S(KC_TAB),KC_ENT,  KC_LSFT, KC_BSPC,    C(KC_LEFT), C(KC_D), C(KC_U), C(KC_RGHT), KC_DEL, _______,
        _______, SK_LGUI, SK_LALT, SK_LCTL, SK_LSFT,CW_TOGG, KC_LEFT,KC_DOWN,KC_UP,KC_RGHT,C(KC_DEL),_______,
        _______, C(KC_Z), C(KC_X), C(KC_C), C(KC_V), _______,    KC_HOME, KC_PGDN, KC_PGUP, KC_END,  C(KC_BSPC),_______,
        _______, _______, _______,                                 _______, _______
    ),

    [_NUM] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,    KC_DOT,  KC_7,    KC_8,    KC_9,    KC_MINS, _______,
        _______, SK_LGUI, SK_LALT, SK_LCTL, SK_LSFT,KC_BSPC, _______,KC_4,KC_5,KC_6,KC_PLUS,_______,
        _______, _______, KC_SLSH, KC_ASTR, KC_EQL,  _______,    _______, KC_1,    KC_2,    KC_3,    _______, _______,
        _______, _______, _______,                                 _______, KC_0
    ),

    [_SYM] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, KC_EXLM, KC_AT,   KC_HASH, KC_DLR,  KC_PERC,    KC_CIRC, KC_AMPR, KC_ASTR, KC_EQL,  KC_QUOT, _______,
        _______, KC_LPRN, KC_LCBR, KC_LBRC, KC_LT,   KC_UNDS,    KC_PIPE, SK_LSFT, SK_LCTL, SK_LALT, SK_LGUI,_______,
        _______, KC_RPRN, KC_RCBR, KC_RBRC, KC_GT,   KC_GRV,     KC_BSLS, KC_TILD, KC_COLN, KC_DQUO, KC_QUES, _______,
        _______, _______, _______,                                 _______, _______
    ),

    [_MOUSE] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, C(KC_Z), C(KC_Y), C(G(KC_LEFT)),C(G(KC_RGHT)),_______, _______, _______, _______, _______, _______, _______,
        _______, _______, KC_MS_L, KC_MS_U, KC_MS_D, KC_MS_R,    _______, SK_LSFT, SK_LCTL, SK_LALT, SK_LGUI,_______,
        _______, _______, KC_WH_L, KC_WH_U, KC_WH_D, KC_WH_R,    _______, _______, _______, _______, _______, _______,
        KC_BTN2, KC_BTN1, _______,                                 _______, _______
    ),

    [_FUN] = LAYOUT(
        QK_BOOTLOADER, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, QK_BOOTLOADER,
        _______, _______,  _______,_______,  _______, _______,    _______, KC_F7,   KC_F8,   KC_F9,   KC_F12,  _______,
        _______, SK_LGUI, SK_LALT, SK_LCTL, SK_LSFT,_______, _______,KC_F4,KC_F5,KC_F6,KC_F11,_______,
        _______, _______, _______, _______, _______, _______,    _______, KC_F1,   KC_F2,   KC_F3,   KC_F10,  _______,
        _______, _______, _______,                                 _______, _______
    ),

    [_NAV_MAC] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, SW_APP_MAC,S(KC_TAB),_______, _______, _______, A(KC_LEFT), C(KC_D), C(KC_U), A(KC_RGHT), KC_DEL, _______,
        _______, SK_LGUI, SK_LALT, SK_LCTL, SK_LSFT, CW_TOGG,    KC_LEFT, KC_DOWN, KC_UP, KC_RGHT, A(KC_DEL),  _______,
        _______, G(KC_Z),  G(KC_X),  G(KC_C),  G(KC_V),  _______,   KC_HOME, KC_PGDN, KC_PGUP, KC_END,  A(KC_BSPC), _______,
        _______, _______, _______,                                 _______, _______
    ),

    [_MEDIA] = LAYOUT(
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,    _______, _______, _______, _______, _______, _______,
        _______, _______, KC_MPRV, KC_VOLD, KC_VOLU, KC_MNXT,    _______, SK_LSFT, SK_LCTL, SK_LALT, SK_LGUI,_______,
        _______, _______, KC_MUTE, KC_BRID, KC_BRIU, KC_MPLY,    _______, _______, _______, _______, _______, _______,
        _______, _______, _______,                                 _______, _______
    ),
};

// 默认 Combo: 每次启动检查，未设置则写入 EEPROM 并刷新 Vial 内存
extern combo_t key_combos[];
extern uint16_t key_combos_keys[][5];

void keyboard_post_init_user(void) {
    static const uint16_t keys[][3] = {
        {KC_S, KC_D, COMBO_END}, {KC_J, KC_K, COMBO_END}, {KC_F, KC_J, COMBO_END},
    };
    static const uint16_t outputs[] = { KC_ESC, KC_LSFT, CW_TOGG };

    vial_combo_entry_t entry;
    for (int i = 0; i < 3; i++) {
        dynamic_keymap_get_combo(i, &entry);
        if (entry.output == 0) {
            memset(&entry, 0, sizeof(entry));
            memcpy(entry.input, keys[i], sizeof(entry.input));
            entry.output = outputs[i];
            dynamic_keymap_set_combo(i, &entry);
        }
        // 刷新 Vial 内存数组 (已于 reload_combo 后执行)
        memcpy(key_combos_keys[i], entry.input, sizeof(entry.input));
        key_combos[i].keys    = key_combos_keys[i];
        key_combos[i].keycode  = entry.output;
    }
}

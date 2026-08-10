// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later
//
// ============================================================================
// 本文件是 vial-qmk 平台驱动
//   platforms/chibios/drivers/vendor/RP/RP2040/serial_vendor.c
// 的本地覆盖版本，来源 commit: dd43959ae5c08d8a28d38a1acf7b04e86b14a344 (2026-07-27)。
//
// 覆盖机制：该文件通过 `QUANTUM_LIB_SRC += serial_$(SERIAL_DRIVER).c` 以裸文件名
// 加入编译，靠 VPATH 解析；build_keyboard.mk 中顺序为
//   KEYMAP_PATH -> USER_PATH -> KEYBOARD_PATHS -> COMMON_VPATH
// 键盘目录先于平台驱动目录，因此这份会覆盖平台版本。依赖仓库本身不需要任何修改。
//
// 相对上游的唯一改动：给两处「osalSysLock() 之内的无超时忙等」加上时间上界。
//   1) enter_rx_state()               —— 等 TX FIFO 排空
//   2) serial_transport_driver_clear() —— 等 RX FIFO 清空
// 原版这两处一旦条件不满足就永久自旋，而此时中断被内核级屏蔽，表现为整机假死：
// 键盘 HID 与 raw HID 同时失效、主机侧仍显示设备已枚举且无任何错误、只有上电复位可恢复。
// 加上界后最坏情况只是放弃当前这次分体事务，协议层已能容忍单次失败
// （见 SPLIT_MAX_CONNECTION_ERRORS / SPLIT_CONNECTION_CHECK_TIMEOUT）。
//
// 维护提醒：这是 vendored 副本，上游更新不会自动跟进。升级 vial-qmk 后请与上游 diff 比对。
// 回滚方式：直接删除本文件即可恢复使用上游版本。
// ============================================================================

#include "serial_usart.h"
#include "serial_protocol.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "wait.h"
#include "debug.h"

/* 忙等的时间上界。
 * TX FIFO 深 8（PIO_FIFO_JOIN_TX），排空一次的理论最坏耗时为
 *   8 字节 x 11 bit(1 起始 + 8 数据 + 1 停止 + 1 余量) / SERIAL_USART_SPEED
 * 默认 SELECT_SOFT_SERIAL_SPEED=1 即 230400bps 时约 382us。这里取 10 倍余量。
 *
 * 计时用 osalOsGetSystemTimeX() / osalTimeDiffX()：本文件本就使用 osal API，
 * 且 X 后缀表示可在 osalSysLock() 临界区内安全调用；osalTimeDiffX 自带回绕处理。
 */
#define SERIAL_PIO_DRAIN_TIMEOUT_US ((8U * 11U * 1000000U * 10U) / SERIAL_USART_SPEED)
#define SERIAL_PIO_DRAIN_TIMEOUT    TIME_US2I(SERIAL_PIO_DRAIN_TIMEOUT_US)

#if !defined(MCU_RP)
#    error PIO Driver is only available for Raspberry Pi 2040 MCUs!
#endif

static inline bool receive_impl(uint8_t* destination, const size_t size, sysinterval_t timeout);
static inline bool send_impl(const uint8_t* source, const size_t size);
static inline void pio_serve_interrupt(void);

#define MSG_PIO_ERROR ((msg_t)(-3))

#if defined(SERIAL_PIO_USE_PIO1)
static const PIO pio = pio1;

OSAL_IRQ_HANDLER(RP_PIO1_IRQ_0_HANDLER) {
    OSAL_IRQ_PROLOGUE();
    pio_serve_interrupt();
    OSAL_IRQ_EPILOGUE();
}
#else
static const PIO pio = pio0;

OSAL_IRQ_HANDLER(RP_PIO0_IRQ_0_HANDLER) {
    OSAL_IRQ_PROLOGUE();
    pio_serve_interrupt();
    OSAL_IRQ_EPILOGUE();
}
#endif

#define UART_TX_WRAP_TARGET 0
#define UART_TX_WRAP 3

// clang-format off
#if defined(SERIAL_USART_FULL_DUPLEX)
static const uint16_t uart_tx_program_instructions[] = {
            //     .wrap_target
    0x9fa0, //  0: pull   block           side 1 [7]
    0xf727, //  1: set    x, 7            side 0 [7]
    0x6001, //  2: out    pins, 1
    0x0642, //  3: jmp    x--, 2                 [6]
            //     .wrap
};
#else
static const uint16_t uart_tx_program_instructions[] = {
            //     .wrap_target
    0x9fa0, //  0: pull   block           side 1 [7]
    0xf727, //  1: set    x, 7            side 0 [7]
    0x6081, //  2: out    pindirs, 1
    0x0642, //  3: jmp    x--, 2                 [6]
            //     .wrap
};
#endif
// clang-format on

static const pio_program_t uart_tx_program = {
    .instructions = uart_tx_program_instructions,
    .length       = 4,
    .origin       = -1,
};

#define UART_RX_WRAP_TARGET 0
#define UART_RX_WRAP 8

// clang-format off
static const uint16_t uart_rx_program_instructions[] = {
            //     .wrap_target
    0x2020, //  0: wait   0 pin, 0
    0xea27, //  1: set    x, 7                   [10]
    0x4001, //  2: in     pins, 1
    0x0642, //  3: jmp    x--, 2                 [6]
    0x00c8, //  4: jmp    pin, 8
    0xc020, //  5: irq    wait 0
    0x20a0, //  6: wait   1 pin, 0
    0x0000, //  7: jmp    0
    0x8020, //  8: push   block
            //     .wrap
};
// clang-format on

static const pio_program_t uart_rx_program = {
    .instructions = uart_rx_program_instructions,
    .length       = 9,
    .origin       = -1,
};

thread_reference_t rx_thread        = NULL;
static int         rx_state_machine = -1;

thread_reference_t tx_thread        = NULL;
static int         tx_state_machine = -1;

void pio_serve_interrupt(void) {
    uint32_t irqs = pio->ints0;

    // The RX FIFO is not empty any more, therefore wake any sleeping rx thread
    if (irqs & (PIO_IRQ0_INTF_SM0_RXNEMPTY_BITS << rx_state_machine)) {
        // Disable rx not empty interrupt
        pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty + rx_state_machine, false);

        osalSysLockFromISR();
        osalThreadResumeI(&rx_thread, MSG_OK);
        osalSysUnlockFromISR();
    }

    // The TX FIFO is not full any more, therefore wake any sleeping tx thread
    if (irqs & (PIO_IRQ0_INTF_SM0_TXNFULL_BITS << tx_state_machine)) {
        // Disable tx not full interrupt
        pio_set_irq0_source_enabled(pio, pis_sm0_tx_fifo_not_full + tx_state_machine, false);
        osalSysLockFromISR();
        osalThreadResumeI(&tx_thread, MSG_OK);
        osalSysUnlockFromISR();
    }

    // IRQ 0 is set on framing or break errors by the rx state machine
    if (pio_interrupt_get(pio, 0UL)) {
        pio_interrupt_clear(pio, 0UL);

        osalSysLockFromISR();
        osalThreadResumeI(&rx_thread, MSG_PIO_ERROR);
        osalSysUnlockFromISR();
    }
}

#if !defined(SERIAL_USART_FULL_DUPLEX)
// The internal pull-ups of the RP2040 are rather weakish with a range of 50k to
// 80k, which in turn do not provide enough current to guarantee fast signal rise
// times with a parasitic capacitance of greater than 100pf. In real world
// applications, like split keyboards which might have vias in the signal path
// or long PCB traces, this prevents a successful communication. The solution
// is to temporarily augment the weak pull ups from the receiving side by
// driving the tx pin high. On the receiving side the lowest possible drive
// strength is chosen because the transmitting side must still be able to drive
// the signal low. With this configuration the rise times are fast enough and
// the generated low level with 360mV will generate a logical zero.
static void __no_inline_not_in_flash_func(enter_rx_state)(void) {
    osalSysLock();
    // Wait for the transmitting state machines FIFO to run empty. At this point
    // the last byte has been pulled from the transmitting state machines FIFO
    // into the output shift register. We have to wait a tiny bit more until
    // this byte is transmitted, before we can turn on the receiving state
    // machine again.
    //
    // 本地改动：上游此处为无超时忙等。若 TX 状态机异常停摆而 FIFO 未排空，
    // 会在屏蔽中断的临界区内永久自旋 => 整机假死。加时间上界后最坏只是丢一次事务。
    const systime_t tx_drain_start = osalOsGetSystemTimeX();
    while (!pio_sm_is_tx_fifo_empty(pio, tx_state_machine)) {
        if (osalTimeDiffX(tx_drain_start, osalOsGetSystemTimeX()) > SERIAL_PIO_DRAIN_TIMEOUT) {
            break;
        }
    }
    // Wait for ~11 bits, 1 start bit + 8 data bits + 1 stop bit + 1 bit
    // headroom.
    wait_us(1000000U * 11U / SERIAL_USART_SPEED);
    // Disable tx state machine to not interfere with our tx pin manipulation
    pio_sm_set_enabled(pio, tx_state_machine, false);
    gpio_set_drive_strength(SERIAL_USART_TX_PIN, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_pins_with_mask(pio, tx_state_machine, 1U << SERIAL_USART_TX_PIN, 1U << SERIAL_USART_TX_PIN);
    pio_sm_set_consecutive_pindirs(pio, tx_state_machine, SERIAL_USART_TX_PIN, 1U, false);
    pio_sm_set_enabled(pio, rx_state_machine, true);
    osalSysUnlock();
}

static void __no_inline_not_in_flash_func(leave_rx_state)(void) {
    osalSysLock();
    // In Half-duplex operation the tx pin dual-functions as sender and
    // receiver. To not receive the data we will send, we disable the receiving
    // state machine.
    pio_sm_set_enabled(pio, rx_state_machine, false);
    pio_sm_set_consecutive_pindirs(pio, tx_state_machine, SERIAL_USART_TX_PIN, 1U, true);
    pio_sm_set_pins_with_mask(pio, tx_state_machine, 0U, 1U << SERIAL_USART_TX_PIN);
    gpio_set_drive_strength(SERIAL_USART_TX_PIN, GPIO_DRIVE_STRENGTH_12MA);
    pio_sm_restart(pio, tx_state_machine);
    pio_sm_set_enabled(pio, tx_state_machine, true);
    osalSysUnlock();
}
#else
// All this trickery is gladly not necessary for full-duplex.
static inline void enter_rx_state(void) {}
static inline void leave_rx_state(void) {}
#endif

/**
 * @brief Clear the FIFO of the RX state machine.
 */
inline void serial_transport_driver_clear(void) {
    osalSysLock();
    // 本地改动：上游此处为无超时循环。若 RX 线被持续噪声或常低驱动，RX 状态机会
    // 不断向 FIFO push，导致该循环在屏蔽中断的临界区内永不退出 => 整机假死。
    const systime_t rx_clear_start = osalOsGetSystemTimeX();
    while (!pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
        pio_sm_clear_fifos(pio, rx_state_machine);
        if (osalTimeDiffX(rx_clear_start, osalOsGetSystemTimeX()) > SERIAL_PIO_DRAIN_TIMEOUT) {
            break;
        }
    }
    osalSysUnlock();
}

static inline msg_t sync_tx(sysinterval_t timeout) {
    msg_t msg = MSG_OK;
    osalSysLock();
    while (pio_sm_is_tx_fifo_full(pio, tx_state_machine)) {
        pio_set_irq0_source_enabled(pio, pis_sm0_tx_fifo_not_full + tx_state_machine, true);
        msg = osalThreadSuspendTimeoutS(&tx_thread, timeout);
        if (msg < MSG_OK) {
            pio_set_irq0_source_enabled(pio, pis_sm0_tx_fifo_not_full + tx_state_machine, false);
            break;
        }
    }
    osalSysUnlock();
    return msg;
}

static inline bool send_impl(const uint8_t* source, const size_t size) {
    size_t send = 0;
    msg_t  msg;
    while (send < size) {
        msg = sync_tx(TIME_MS2I(SERIAL_USART_TIMEOUT));
        if (msg < MSG_OK) {
            return false;
        }

        osalSysLock();
        while (send < size) {
            if (pio_sm_is_tx_fifo_full(pio, tx_state_machine)) {
                break;
            }
            if (send >= size) {
                break;
            }
            pio_sm_put(pio, tx_state_machine, (uint32_t)(*source));
            source++;
            send++;
        }
        osalSysUnlock();
    }

    return send == size;
}

/**
 * @brief Blocking send of buffer with timeout.
 *
 * @return true Send success.
 * @return false Send failed.
 */
inline bool serial_transport_send(const uint8_t* source, const size_t size) {
    leave_rx_state();
    bool result = send_impl(source, size);
    enter_rx_state();

    return result;
}

static inline msg_t sync_rx(sysinterval_t timeout) {
    msg_t msg = MSG_OK;
    osalSysLock();
    while (pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
        pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty + rx_state_machine, true);
        msg = osalThreadSuspendTimeoutS(&rx_thread, timeout);
        if (msg < MSG_OK) {
            pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty + rx_state_machine, false);
            break;
        }
    }
    osalSysUnlock();
    return msg;
}

static inline bool receive_impl(uint8_t* destination, const size_t size, sysinterval_t timeout) {
    size_t read = 0U;

    while (read < size) {
        msg_t msg = sync_rx(timeout);
        if (msg < MSG_OK) {
            return false;
        }
        osalSysLock();
        while (true) {
            if (pio_sm_is_rx_fifo_empty(pio, rx_state_machine)) {
                break;
            }
            if (read >= size) {
                break;
            }
            *destination++ = *((uint8_t*)&pio->rxf[rx_state_machine] + 3U);
            read++;
        }
        osalSysUnlock();
    }

    return read == size;
}

/**
 * @brief  Blocking receive of size * bytes with timeout.
 *
 * @return true Receive success.
 * @return false Receive failed, e.g. by timeout.
 */
inline bool serial_transport_receive(uint8_t* destination, const size_t size) {
    return receive_impl(destination, size, TIME_MS2I(SERIAL_USART_TIMEOUT));
}

/**
 * @brief  Blocking receive of size * bytes.
 *
 * @return true Receive success.
 * @return false Receive failed.
 */
inline bool serial_transport_receive_blocking(uint8_t* destination, const size_t size) {
    return receive_impl(destination, size, TIME_INFINITE);
}

static inline void pio_tx_init(pin_t tx_pin) {
    uint pio_idx = pio_get_index(pio);
    uint offset  = pio_add_program(pio, &uart_tx_program);

#if defined(SERIAL_USART_FULL_DUPLEX)
    // clang-format off
    iomode_t tx_pin_mode = PAL_RP_GPIO_OE |
                           PAL_RP_PAD_SLEWFAST |
                           PAL_RP_PAD_DRIVE4 |
                           (pio_idx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on
    pio_sm_set_pins_with_mask(pio, tx_state_machine, 1U << tx_pin, 1U << tx_pin);
    pio_sm_set_consecutive_pindirs(pio, tx_state_machine, tx_pin, 1U, true);
#else
    // clang-format off
    iomode_t tx_pin_mode = PAL_RP_PAD_IE |
                           PAL_RP_GPIO_OE |
                           PAL_RP_PAD_SCHMITT |
                           PAL_RP_PAD_PUE |
                           PAL_RP_PAD_SLEWFAST |
                           PAL_RP_PAD_DRIVE12 |
                           PAL_RP_IOCTRL_OEOVER_DRVINVPERI |
                           (pio_idx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on
    pio_sm_set_pins_with_mask(pio, tx_state_machine, 0U << tx_pin, 1U << tx_pin);
    pio_sm_set_consecutive_pindirs(pio, tx_state_machine, tx_pin, 1U, true);
#endif

    palSetLineMode(tx_pin, tx_pin_mode);

    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offset + UART_TX_WRAP_TARGET, offset + UART_TX_WRAP);
#if defined(SERIAL_USART_FULL_DUPLEX)
    sm_config_set_sideset(&config, 2, true, false);
#else
    sm_config_set_sideset(&config, 2, true, true);
#endif
    // OUT shifts to right, no autopull
    sm_config_set_out_shift(&config, true, false, 32);
    // We are mapping both OUT and side-set to the same pin, because sometimes
    // we need to assert user data onto the pin (with OUT) and sometimes
    // assert constant values (start/stop bit)
    sm_config_set_out_pins(&config, tx_pin, 1);
    sm_config_set_sideset_pins(&config, tx_pin);
    // We only need TX, so get an 8-deep FIFO!
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    // SM transmits 1 bit per 8 execution cycles.
    float div = (float)clock_get_hz(clk_sys) / (8 * SERIAL_USART_SPEED);
    sm_config_set_clkdiv(&config, div);
    pio_sm_init(pio, tx_state_machine, offset, &config);
    pio_sm_set_enabled(pio, tx_state_machine, true);
}

static inline void pio_rx_init(pin_t rx_pin) {
    uint offset = pio_add_program(pio, &uart_rx_program);

#if defined(SERIAL_USART_FULL_DUPLEX)
    uint pio_idx = pio_get_index(pio);
    pio_sm_set_consecutive_pindirs(pio, rx_state_machine, rx_pin, 1, false);
    // clang-format off
    iomode_t rx_pin_mode = PAL_RP_PAD_IE |
                           PAL_RP_PAD_SCHMITT |
                           PAL_RP_PAD_PUE |
                           (pio_idx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on
    palSetLineMode(rx_pin, rx_pin_mode);
#endif

    pio_sm_config config = pio_get_default_sm_config();
    sm_config_set_wrap(&config, offset + UART_RX_WRAP_TARGET, offset + UART_RX_WRAP);
    sm_config_set_in_pins(&config, rx_pin); // for WAIT, IN
    sm_config_set_jmp_pin(&config, rx_pin); // for JMP
    // Shift to right, autopush disabled
    sm_config_set_in_shift(&config, true, false, 32);
    // Deeper FIFO as we're not doing any TX
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    // SM transmits 1 bit per 8 execution cycles.
    float div = (float)clock_get_hz(clk_sys) / (8 * SERIAL_USART_SPEED);
    sm_config_set_clkdiv(&config, div);
    pio_sm_init(pio, rx_state_machine, offset, &config);
    pio_sm_set_enabled(pio, rx_state_machine, true);
}

static inline void pio_init(pin_t tx_pin, pin_t rx_pin) {
    uint pio_idx = pio_get_index(pio);

    /* Get PIOx peripheral out of reset state. */
    hal_lld_peripheral_unreset(pio_idx == 0 ? RESETS_ALLREG_PIO0 : RESETS_ALLREG_PIO1);

    tx_state_machine = pio_claim_unused_sm(pio, true);
    if (tx_state_machine < 0) {
        dprintln("ERROR: Failed to acquire state machine for serial transmission!");
        return;
    }
    pio_tx_init(tx_pin);

    rx_state_machine = pio_claim_unused_sm(pio, true);
    if (rx_state_machine < 0) {
        dprintln("ERROR: Failed to acquire state machine for serial reception!");
        return;
    }
    pio_rx_init(rx_pin);

    // Enable error flag IRQ source for rx state machine
    pio_set_irq0_source_enabled(pio, pis_sm0_rx_fifo_not_empty + rx_state_machine, true);
    pio_set_irq0_source_enabled(pio, pis_sm0_tx_fifo_not_full + tx_state_machine, true);
    pio_set_irq0_source_enabled(pio, pis_interrupt0, true);

    // Enable PIO specific interrupt vector, as the pio implementation is timing
    // critical we use the highest possible priority.
#if defined(SERIAL_PIO_USE_PIO1)
    nvicEnableVector(RP_PIO1_IRQ_0_NUMBER, CORTEX_MAX_KERNEL_PRIORITY);
#else
    nvicEnableVector(RP_PIO0_IRQ_0_NUMBER, CORTEX_MAX_KERNEL_PRIORITY);
#endif

    enter_rx_state();
}

/**
 * @brief PIO driver specific initialization function for the master side.
 */
void serial_transport_driver_master_init(void) {
#if defined(SERIAL_USART_FULL_DUPLEX)
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_RX_PIN;
#else
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_TX_PIN;
#endif

#if defined(SERIAL_USART_PIN_SWAP)
    pio_init(rx_pin, tx_pin);
#else
    pio_init(tx_pin, rx_pin);
#endif
}

/**
 * @brief PIO driver specific initialization function for the slave side.
 */
void serial_transport_driver_slave_init(void) {
#if defined(SERIAL_USART_FULL_DUPLEX)
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_RX_PIN;
#else
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_TX_PIN;
#endif

    pio_init(tx_pin, rx_pin);
}

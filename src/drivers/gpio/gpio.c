/**
 * @file gpio.c
 * @brief GPIO Driver Implementation
 *      This file provides high-level functions to interact with the GPIO
 * controller. It abstracts away the register manipulations and provides a clean
 * API for initializing pins, setting/clearing outputs, reading inputs, and
 * configuring interrupts.
 *
 * @note This code is designed for educational purposes to demonstrate FW access
 * to HW conceptse.
 */
#include <stdint.h>

#include "gpio_demo_regs.h"
#include "gpio.h"
#include "log.h"
#include "log_marker.h"

// Pointer to the memory-mapped GPIO controller registers
static volatile gpio_ctrl_t *regs = nullptr;

/**
 * @brief Set the base address for the GPIO registers
 * @param base_addr Pointer to the memory-mapped GPIO controller registers
 */
void gpio_set_regs(volatile gpio_ctrl_t *base_addr)
{
    regs = base_addr;
}

/**
 * @brief Get the pointer to the GPIO registers
 * @return Pointer to the memory-mapped GPIO controller registers
 */
volatile gpio_ctrl_t *gpio_get_regs(void)
{
    return regs;
}

/**
 * @brief Initialize a GPIO pin
 * @param regs Pointer to the GPIO base address
 * @param pin Pin number (0-15)
 * @param is_output True for Output, False for Input
 */
void gpio_init_pin(volatile gpio_ctrl_t *regs, uint16_t pin, bool is_output)
{
    LOG_GPIO_DEBUG("Initializing Pin %d as %s", pin, is_output ? "Output" : "Input");
    if (is_output) {
        regs->dir.f.pin_dir |= (1 << pin);
    } else {
        regs->dir.f.pin_dir &= ~(1 << pin);
    }
}

/**
 * @brief Initialize multiple GPIO pins using a bitmask
 * @param regs Pointer to the GPIO base address
 * @param pending_mask Bitmask of pins to initialize (e.g., 0x05 initializes
 * pins 0 and 2)
 * @param is_output True for Output, False for Input
 */
void gpio_init_pin_mask(volatile gpio_ctrl_t *regs, uint16_t pending_mask, bool is_output)
{
    LOG_GPIO_DEBUG("Initializing Pins Mask 0x%02X as %s", pending_mask, is_output ? "Output" : "Input");
    if (is_output) {
        regs->dir.f.pin_dir |= pending_mask;
    } else {
        regs->dir.f.pin_dir &= ~pending_mask;
    }
}

/**
 * @brief Set an output pin HIGH (Atomic)
 */
void gpio_set_out_pin(volatile gpio_ctrl_t *regs, uint16_t pin)
{
    LOG_GPIO_DEBUG("Setting Pin %d HIGH", pin);
    regs->set.f.set_bits = (1 << pin);
#ifndef USE_HW
    gpio_ctrl_R_out_t out = regs->out; // Read back the out register to ensure
                                       // the write has taken effect
    if ((out.f.pin_out & (1 << pin)) == 0) {
        regs->out.f.pin_out |= (1 << pin);
    }
#endif
}

/**
 * @brief Clear an output pin LOW (Atomic)
 */
void gpio_clear_out_pin(volatile gpio_ctrl_t *regs, uint16_t pin)
{
    LOG_GPIO_DEBUG("Setting Pin %d LOW", pin);
    regs->clr.f.clr_bits = (1 << pin);
#ifndef USE_HW
    gpio_ctrl_R_out_t out = regs->out; // Read back the out register to ensure
                                       // the clear has taken effect
    if ((out.f.pin_out & (1 << pin)) != 0) {
        regs->out.f.pin_out &= ~(1 << pin);
    }
#endif
}

/**
 * @brief Read the current physical state of a pin
 */
bool gpio_read_in_pin(volatile gpio_ctrl_t *regs, uint16_t pin)
{
    LOG_GPIO_DEBUG("Reading Pin %d state: %s", pin, (regs->in.all & (1u << pin)) ? "true" : "false");
    return (regs->in.all & (1u << pin)) != 0;
}

/**
 * @brief Read the state of all pins at once
 * @return 8-bit value representing the state of pins
 */
uint8_t gpio_get_all_in_pins(volatile gpio_ctrl_t *regs)
{
    return (uint8_t)regs->in.f.pin_in;
}

/**
 * @brief Check if the alarm bit is set in the input register
 * @return true if the alarm bit is set, false otherwise
 */
bool gpio_is_alarm(volatile gpio_ctrl_t *regs)
{
    return (regs->in.f.alarm != 0) || (regs->int_sts.f.status & (1 << GPIO_CTRL_R_IN_F_ALARM_BP));
}

void gpio_enable_interrupt(volatile gpio_ctrl_t *regs, uint16_t pin, uint8_t type)
{
    LOG_GPIO_DEBUG("Enabling Interrupt on Pin %d, Type: %s", pin, type ? "Edge" : "Level");
    // Set type (0: Level, 1: Edge)
    if (type) {
        regs->int_type.f.typ |= (1 << pin);
    } else {
        regs->int_type.f.typ &= ~(1 << pin);
    }

    // Enable the interrupt
    regs->int_en.f.en |= (1 << pin);
}

/**
 * @brief Check and Clear interrupt status (W1C)
 * @return 8-bit mask of pins that triggered an interrupt
 */
uint8_t gpio_get_and_clear_irq_status(volatile gpio_ctrl_t *regs)
{
    uint16_t status = regs->int_sts.f.status;

    LOG_GPIO_DEBUG("Interrupt Status Read: 0x%02X", status);

    // Write 1 to clear (W1C) - we write the status back to clear active bits
    regs->int_sts.f.status = status;
#ifndef USE_HW
    gpio_ctrl_int_R_sts_t int_sts = regs->int_sts; // Read back the status to verify
    if ((int_sts.f.status & status) != 0) {
        regs->int_sts.all &= ~status; // Clear any bits that weren't cleared by the W1C write
    }
#endif
    return status;
}

/**
 * @brief Configure Level Interrupt Polarity
 * @param active_low If true, interrupt triggers when pin is 0. If false, when
 * pin is 1.
 */
void gpio_set_interrupt_polarity(volatile gpio_ctrl_t *regs, uint16_t pin, bool active_low)
{
    LOG_GPIO_DEBUG("Setting Interrupt Polarity on Pin %d to %s", pin, active_low ? "Active Low" : "Active High");
    if (active_low) {
        regs->int_pol.f.pol |= (1 << pin);
    } else {
        regs->int_pol.f.pol &= ~(1 << pin);
    }
}

/**
 * @brief Clear a specific pin's interrupt status (W1C)
 * @param regs Pointer to the GPIO base address
 * @param pin  Pin number (0-15)
 */
void gpio_clear_interrupt(volatile gpio_ctrl_t *regs, uint16_t pin)
{
    LOG_GPIO_DEBUG("Clearing Interrupt Status for Pin %d", pin);
    regs->int_sts.f.status = (1 << pin);
#ifndef USE_HW
    gpio_ctrl_int_R_sts_t int_sts = regs->int_sts; // Read back the status to verify
    if ((int_sts.f.status & (1 << pin)) != 0) {
        // Clear the bit if it wasn't cleared by the W1C write
        regs->int_sts.all &= ~(1 << pin);
    }
#endif
}

/**
 * @brief Clear multiple interrupts simultaneously
 * @param pending_mask Bitmask of pins to clear (e.g., 0x48 clears pins 3 and 6)
 */
void gpio_clear_interrupts_mask(volatile gpio_ctrl_t *regs, uint16_t pending_mask)
{
    LOG_GPIO_DEBUG("Clearing Interrupt Status for Pins Mask 0x%02X", pending_mask);
    // Write the pending_mask to clear the specified bits (W1C)
    regs->int_sts.f.status = pending_mask;
#ifndef USE_HW
    gpio_ctrl_int_R_sts_t int_sts = regs->int_sts; // Read back the status to verify
    if ((int_sts.f.status & pending_mask) != 0) {
        regs->int_sts.all &= ~pending_mask;
    }
#endif
}

/**
 * @brief Stage 1: Hardware Reset (Call ONCE at boot/sim-start)
 * Resets the controller to a known safe state:
 * All pins are Inputs (Hi-Z), Interrupts are disabled and cleared.
 */
void gpio_init_controller(volatile gpio_ctrl_t *regs)
{
    LOG_GPIO_DEBUG("Initializing GPIO Controller to default state");
    // Set all pins to Input (Safe state)
    regs->dir.f.pin_dir = 0x00;

    // Disable all interrupts
    regs->int_en.f.en = 0x0000;

    // Clear any stale interrupt status (W1C)
    regs->int_sts.all = 0xFFFF; // Clear all status bits
#ifndef USE_HW
    // Read back the status to verify that all bits are cleared
    gpio_ctrl_int_R_sts_t int_sts = regs->int_sts;
    if (int_sts.f.status != 0) {
        regs->int_sts.all &= ~0xFFFF;
    }
#endif

    // Set default polarity (Active High) and type (Level)
    regs->int_pol.f.pol = 0x0000;  // Active High
    regs->int_type.f.typ = 0x0000; // Level-triggered

    // Initialize Watchdog to a safe disabled state
    regs->wdt_cfg.f.prescale = 0;
    regs->wdt_val.f.timer = 0;
    regs->wdt_reload.f.reload = 0;
    regs->wdt_cfg.f.en = 0; // Watchdog disabled by default
}

/**
 * @brief Configure an interrupt for a specific pin with
 *        the desired type (Level or Edge)
 * @param regs Pointer to the GPIO base address
 * @param pin Pin number (0-15)
 * @param type Interrupt type: 0 for Level, 1 for Edge
 */
void gpio_configure_interrupt(volatile gpio_ctrl_t *regs, uint16_t pin, bool active_low)
{
    LOG_GPIO_DEBUG("Configuring Interrupt for Pin %d, Active Low: %s", pin, active_low ? "Yes" : "No");
    // Set Polarity
    if (active_low) {
        regs->int_pol.f.pol |= (1 << pin);
    } else {
        regs->int_pol.f.pol &= ~(1 << pin);
    }

    // Enable the interrupt mask
    regs->int_en.f.en |= (1 << pin);
}

/**
 * @brief Configure Watchdog
 * @param timeout_ms Desired timeout in milliseconds
 */
void gpio_wdt_setup(volatile gpio_ctrl_t *regs, uint32_t timeout_ms)
{
    LOG_GPIO_DEBUG("Configuring Watchdog with timeout %d ms", timeout_ms);
    if (timeout_ms < 255) {
        regs->wdt_cfg.f.prescale = 0; // 1ms ticks
        regs->wdt_reload.f.reload = (uint8_t)timeout_ms;
    } else {
        regs->wdt_cfg.f.prescale = 1; // 10ms ticks
        regs->wdt_reload.f.reload = (uint8_t)(timeout_ms / 10);
    }
    regs->wdt_val.f.timer = regs->wdt_reload.f.reload; // Initialize timer to the reload value
    regs->wdt_cfg.f.en = 1;                            // Enable the Watchdog
}

/**
 * @brief Kick the Watchdog to prevent timeout
 */
void gpio_wdt_kick(volatile gpio_ctrl_t *regs)
{
    LOG_GPIO_DEBUG("Kicking Watchdog. Timer reset to %d ms", regs->wdt_reload.f.reload);
    if (regs->wdt_cfg.f.en == 0) {
        LOG_GPIO_WARNING("Attempted to kick Watchdog while it is disabled!");
        return;
    }
    // Restart the timer with the pre-configured reload value to prevent it from
    // reaching zero and triggering a timeout
    regs->wdt_val.f.timer = regs->wdt_reload.f.reload;
}
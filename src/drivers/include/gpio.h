#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// GPIO Driver Interface
volatile gpio_ctrl_t *gpio_get_regs(void);
// dummy space for clang-formater to not indent to previous function name column
void    gpio_set_regs(volatile gpio_ctrl_t *base_addr);
void    gpio_init_pin(volatile gpio_ctrl_t *regs, uint16_t pin, bool is_output);
void    gpio_init_pin_mask(volatile gpio_ctrl_t *regs, uint16_t pending_mask, bool is_output);
void    gpio_set_out_pin(volatile gpio_ctrl_t *regs, uint16_t pin);
void    gpio_clear_out_pin(volatile gpio_ctrl_t *regs, uint16_t pin);
bool    gpio_read_in_pin(volatile gpio_ctrl_t *regs, uint16_t pin);
uint8_t gpio_get_all_in_pins(volatile gpio_ctrl_t *regs);
bool    gpio_is_alarm(volatile gpio_ctrl_t *regs);
void    gpio_configure_interrupt(volatile gpio_ctrl_t *regs, uint16_t pin, bool active_low);
void    gpio_enable_interrupt(volatile gpio_ctrl_t *regs, uint16_t pin, uint8_t type);
uint8_t gpio_get_and_clear_irq_status(volatile gpio_ctrl_t *regs);
void    gpio_set_interrupt_polarity(volatile gpio_ctrl_t *regs, uint16_t pin, bool active_low);
void    gpio_clear_interrupt(volatile gpio_ctrl_t *regs, uint16_t pin);
void    gpio_clear_interrupts_mask(volatile gpio_ctrl_t *regs, uint16_t pending_mask);
void    gpio_init_controller(volatile gpio_ctrl_t *regs);
void    gpio_wdt_setup(volatile gpio_ctrl_t *regs, uint32_t timeout_ms);
void    gpio_wdt_kick(volatile gpio_ctrl_t *regs);

#ifdef __cplusplus
}
#endif

// Logging Macros for GPIO Driver
#define LOG_GPIO_ERROR(...) LOG_ENTITY_ERROR(ID_DEV(ENT_GPIO), __VA_ARGS__)
#define LOG_GPIO_WARNING(...) LOG_ENTITY_WARNING(ID_DEV(ENT_GPIO), __VA_ARGS__)
#define LOG_GPIO_INFO(...) LOG_ENTITY_INFO(ID_DEV(ENT_GPIO), __VA_ARGS__)
#define LOG_GPIO_DEBUG(...) LOG_ENTITY_DEBUG(ID_DEV(ENT_GPIO), __VA_ARGS__)
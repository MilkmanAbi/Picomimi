/**
 * Picomimi Hardware Drivers
 * 
 * UART, GPIO, Timer, IRQ for RP2040/RP2350
 */

#include "picomimi.h"

// ============================================================================
// RP2040 REGISTER BASES
// ============================================================================

#define RESETS_BASE         0x4000C000
#define IO_BANK0_BASE       0x40014000
#define PADS_BANK0_BASE     0x4001C000
#define SIO_BASE            0xD0000000
#define UART0_BASE          0x40034000
#define UART1_BASE          0x40038000
#define TIMER_BASE          0x40054000
#define WATCHDOG_BASE       0x40058000
#define CLOCKS_BASE         0x40008000
#define XOSC_BASE           0x40024000
#define PLL_SYS_BASE        0x40028000
#define PPB_BASE            0xE0000000

// NVIC registers
#define NVIC_ISER           ((volatile u32 *)(PPB_BASE + 0xE100))
#define NVIC_ICER           ((volatile u32 *)(PPB_BASE + 0xE180))
#define NVIC_ISPR           ((volatile u32 *)(PPB_BASE + 0xE200))
#define NVIC_IPR            ((volatile u32 *)(PPB_BASE + 0xE400))
#define SCB_ICSR            ((volatile u32 *)(PPB_BASE + 0xED04))

// SysTick
#define SYST_CSR            ((volatile u32 *)(PPB_BASE + 0xE010))
#define SYST_RVR            ((volatile u32 *)(PPB_BASE + 0xE014))
#define SYST_CVR            ((volatile u32 *)(PPB_BASE + 0xE018))

// ============================================================================
// CLOCK CONFIGURATION (simplified)
// ============================================================================

#define SYS_CLK_HZ          125000000   // 125 MHz

static void clock_init(void) {
    // This is simplified - real init is more complex
    // Would configure XOSC, PLL, etc.
    
    // Enable all peripherals in RESETS
    volatile u32 *resets = (volatile u32 *)RESETS_BASE;
    resets[0] = 0;  // RESET register - clear all
    
    // Wait for reset done
    while ((resets[2] & 0x01FFFFFF) != 0x01FFFFFF);
}

// ============================================================================
// GPIO DRIVER
// ============================================================================

#define GPIO_CTRL(pin)      ((volatile u32 *)(IO_BANK0_BASE + 0x004 + (pin) * 8))
#define GPIO_STATUS(pin)    ((volatile u32 *)(IO_BANK0_BASE + 0x000 + (pin) * 8))
#define PADS_GPIO(pin)      ((volatile u32 *)(PADS_BANK0_BASE + 0x004 + (pin) * 4))

#define SIO_GPIO_OUT        ((volatile u32 *)(SIO_BASE + 0x010))
#define SIO_GPIO_OUT_SET    ((volatile u32 *)(SIO_BASE + 0x014))
#define SIO_GPIO_OUT_CLR    ((volatile u32 *)(SIO_BASE + 0x018))
#define SIO_GPIO_OUT_XOR    ((volatile u32 *)(SIO_BASE + 0x01C))
#define SIO_GPIO_OE         ((volatile u32 *)(SIO_BASE + 0x020))
#define SIO_GPIO_OE_SET     ((volatile u32 *)(SIO_BASE + 0x024))
#define SIO_GPIO_OE_CLR     ((volatile u32 *)(SIO_BASE + 0x028))
#define SIO_GPIO_IN         ((volatile u32 *)(SIO_BASE + 0x004))

void gpio_init(u32 pin, bool output) {
    if (pin > 29) return;
    
    // Set function to SIO (5)
    *GPIO_CTRL(pin) = 5;
    
    // Configure pad
    *PADS_GPIO(pin) = (1 << 6) |  // Input enable
                      (output ? 0 : (1 << 3));  // Pull-up if input
    
    // Set direction
    if (output) {
        *SIO_GPIO_OE_SET = (1 << pin);
    } else {
        *SIO_GPIO_OE_CLR = (1 << pin);
    }
}

void gpio_set(u32 pin, bool value) {
    if (pin > 29) return;
    
    if (value) {
        *SIO_GPIO_OUT_SET = (1 << pin);
    } else {
        *SIO_GPIO_OUT_CLR = (1 << pin);
    }
}

bool gpio_get(u32 pin) {
    if (pin > 29) return false;
    return (*SIO_GPIO_IN >> pin) & 1;
}

void gpio_toggle(u32 pin) {
    if (pin > 29) return;
    *SIO_GPIO_OUT_XOR = (1 << pin);
}

// ============================================================================
// UART DRIVER
// ============================================================================

#define UART_DR(base)       ((volatile u32 *)((base) + 0x000))
#define UART_FR(base)       ((volatile u32 *)((base) + 0x018))
#define UART_IBRD(base)     ((volatile u32 *)((base) + 0x024))
#define UART_FBRD(base)     ((volatile u32 *)((base) + 0x028))
#define UART_LCR_H(base)    ((volatile u32 *)((base) + 0x02C))
#define UART_CR(base)       ((volatile u32 *)((base) + 0x030))
#define UART_IMSC(base)     ((volatile u32 *)((base) + 0x038))

#define UART_FR_TXFF        (1 << 5)  // TX FIFO full
#define UART_FR_RXFE        (1 << 4)  // RX FIFO empty

static u32 uart_base = UART0_BASE;

void uart_init(u32 baud) {
    // Configure GPIO pins for UART0 (TX=0, RX=1)
    *GPIO_CTRL(0) = 2;  // UART function
    *GPIO_CTRL(1) = 2;
    
    // Calculate baud divisor
    // BAUDDIV = UARTCLK / (16 * baud)
    // UARTCLK = 125MHz (assuming peri_clk)
    u32 div = (SYS_CLK_HZ * 4) / baud;  // Fixed point 6.6
    u32 ibrd = div >> 6;
    u32 fbrd = div & 0x3F;
    
    // Disable UART
    *UART_CR(uart_base) = 0;
    
    // Set baud rate
    *UART_IBRD(uart_base) = ibrd;
    *UART_FBRD(uart_base) = fbrd;
    
    // 8N1, enable FIFO
    *UART_LCR_H(uart_base) = (3 << 5) | (1 << 4);
    
    // Enable UART, TX, RX
    *UART_CR(uart_base) = (1 << 0) | (1 << 8) | (1 << 9);
}

void uart_putc(char c) {
    while (*UART_FR(uart_base) & UART_FR_TXFF);
    *UART_DR(uart_base) = c;
    
    if (c == '\n') uart_putc('\r');
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

int uart_getc(void) {
    while (*UART_FR(uart_base) & UART_FR_RXFE);
    return *UART_DR(uart_base) & 0xFF;
}

bool uart_readable(void) {
    return !(*UART_FR(uart_base) & UART_FR_RXFE);
}

// ============================================================================
// TIMER DRIVER
// ============================================================================

#define TIMER_TIMEHR        ((volatile u32 *)(TIMER_BASE + 0x08))
#define TIMER_TIMELR        ((volatile u32 *)(TIMER_BASE + 0x0C))
#define TIMER_ALARM0        ((volatile u32 *)(TIMER_BASE + 0x10))
#define TIMER_ARMED         ((volatile u32 *)(TIMER_BASE + 0x20))
#define TIMER_INTR          ((volatile u32 *)(TIMER_BASE + 0x34))
#define TIMER_INTE          ((volatile u32 *)(TIMER_BASE + 0x38))

static volatile tick_t system_ticks = 0;
static u32 tick_interval_us = 1000;  // 1ms default

void timer_init(u32 hz) {
    tick_interval_us = 1000000 / hz;
    
    // Use SysTick for tick counter
    *SYST_RVR = (SYS_CLK_HZ / hz) - 1;
    *SYST_CVR = 0;
    *SYST_CSR = 0x07;  // Enable, interrupt, use processor clock
}

tick_t timer_ticks(void) {
    return system_ticks;
}

void timer_delay_us(u32 us) {
    u32 start = *TIMER_TIMELR;
    while ((*TIMER_TIMELR - start) < us);
}

void timer_delay_ms(u32 ms) {
    timer_delay_us(ms * 1000);
}

u64 timer_time_us(void) {
    u32 hi = *TIMER_TIMEHR;
    u32 lo = *TIMER_TIMELR;
    return ((u64)hi << 32) | lo;
}

// SysTick handler
void SysTick_Handler(void) {
    system_ticks++;
    
    // Check wakeups
    extern void task_check_wakeups(tick_t);
    task_check_wakeups(system_ticks);
    
    // Scheduler tick
    extern void sched_tick(void);
    sched_tick();
}

// ============================================================================
// IRQ DRIVER
// ============================================================================

typedef void (*irq_handler_t)(void);
static irq_handler_t irq_handlers[32];

void irq_init(void) {
    // Clear all pending
    NVIC_ICSR[0] = 0xFFFFFFFF;
    
    // Set all priorities to default
    for (int i = 0; i < 8; i++) {
        NVIC_IPR[i] = 0x80808080;  // Middle priority
    }
}

void irq_enable(u32 irq) {
    if (irq < 32) {
        NVIC_ISER[0] = (1 << irq);
    }
}

void irq_disable(u32 irq) {
    if (irq < 32) {
        NVIC_ICER[0] = (1 << irq);
    }
}

void irq_set_handler(u32 irq, void (*handler)(void)) {
    if (irq < 32) {
        irq_handlers[irq] = handler;
    }
}

void irq_set_priority(u32 irq, u8 priority) {
    if (irq < 32) {
        volatile u8 *prio = (volatile u8 *)&NVIC_IPR[irq / 4];
        prio[irq % 4] = priority;
    }
}

// Generic IRQ dispatcher
void Default_Handler(void) {
    // Get active IRQ number
    u32 irq = (*SCB_ICSR & 0xFF) - 16;
    
    if (irq < 32 && irq_handlers[irq]) {
        irq_handlers[irq]();
    }
}

// ============================================================================
// LED HELPER
// ============================================================================

#define LED_PIN     25

void led_init(void) {
    gpio_init(LED_PIN, true);
}

void led_on(void) {
    gpio_set(LED_PIN, true);
}

void led_off(void) {
    gpio_set(LED_PIN, false);
}

void led_toggle(void) {
    gpio_toggle(LED_PIN);
}

// ============================================================================
// ADC DRIVER (for temperature sensor)
// ============================================================================

#define ADC_BASE            0x4004C000
#define ADC_CS              ((volatile u32 *)(ADC_BASE + 0x00))
#define ADC_RESULT          ((volatile u32 *)(ADC_BASE + 0x04))
#define ADC_FCS             ((volatile u32 *)(ADC_BASE + 0x08))
#define ADC_FIFO            ((volatile u32 *)(ADC_BASE + 0x0C))

void adc_init(void) {
    // Enable ADC
    *ADC_CS = (1 << 0);  // EN
    while (!(*ADC_CS & (1 << 8)));  // Wait for ready
}

u16 adc_read(u8 channel) {
    // Select channel and start conversion
    *ADC_CS = (*ADC_CS & ~0x07000) | ((channel & 7) << 12) | (1 << 2);
    
    // Wait for conversion
    while (!(*ADC_CS & (1 << 8)));
    
    return *ADC_RESULT & 0xFFF;
}

float temp_read(void) {
    // Channel 4 is temperature sensor
    u16 raw = adc_read(4);
    
    // Convert to temperature
    // T = 27 - (ADC_voltage - 0.706) / 0.001721
    float voltage = raw * 3.3f / 4096.0f;
    return 27.0f - (voltage - 0.706f) / 0.001721f;
}

// ============================================================================
// WATCHDOG
// ============================================================================

#define WD_CTRL             ((volatile u32 *)(WATCHDOG_BASE + 0x00))
#define WD_LOAD             ((volatile u32 *)(WATCHDOG_BASE + 0x04))
#define WD_TICK             ((volatile u32 *)(WATCHDOG_BASE + 0x2C))

void watchdog_init(u32 timeout_ms) {
    // Enable watchdog tick generator
    *WD_TICK = (12 << 0) | (1 << 9);  // 12 cycles per tick, enable
    
    // Set timeout
    *WD_LOAD = timeout_ms * 1000;
    
    // Enable
    *WD_CTRL = (1 << 30);  // Enable
}

void watchdog_feed(void) {
    *WD_LOAD = *WD_LOAD;  // Reload
}

void watchdog_reboot(void) {
    *WD_CTRL = (1 << 31);  // Force reboot
    while (1);
}

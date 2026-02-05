/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC Syscall Interface                                                  ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Low-level syscall interface for user programs                            ║
 * ║  All hardware access goes through these syscalls                          ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef MIMIC_SYSCALL_H
#define MIMIC_SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// SYSCALL NUMBERS (must match kernel)
// ============================================================================

#define SYS_EXIT            0
#define SYS_YIELD           1
#define SYS_SLEEP           2
#define SYS_TIME            3

#define SYS_MALLOC          10
#define SYS_FREE            11
#define SYS_REALLOC         12

#define SYS_OPEN            20
#define SYS_CLOSE           21
#define SYS_READ            22
#define SYS_WRITE           23
#define SYS_SEEK            24

#define SYS_PUTCHAR         30
#define SYS_GETCHAR         31
#define SYS_PUTS            32

#define SYS_GPIO_INIT       40
#define SYS_GPIO_DIR        41
#define SYS_GPIO_PUT        42
#define SYS_GPIO_GET        43
#define SYS_GPIO_PULL       44

#define SYS_PWM_INIT        50
#define SYS_PWM_SET_WRAP    51
#define SYS_PWM_SET_LEVEL   52
#define SYS_PWM_ENABLE      53

#define SYS_ADC_INIT        60
#define SYS_ADC_SELECT      61
#define SYS_ADC_READ        62
#define SYS_ADC_TEMP        63

#define SYS_SPI_INIT        70
#define SYS_SPI_WRITE       71
#define SYS_SPI_READ        72
#define SYS_SPI_TRANSFER    73

#define SYS_I2C_INIT        80
#define SYS_I2C_WRITE       81
#define SYS_I2C_READ        82

// ============================================================================
// SYSCALL MECHANISM
// ============================================================================

// ARM Cortex-M syscall via SVC instruction
// Arguments in r0-r3, syscall number in r7, return in r0

static inline int32_t __syscall0(uint32_t num) {
    register uint32_t r7 __asm__("r7") = num;
    register int32_t r0 __asm__("r0");
    
    __asm__ volatile (
        "svc #0"
        : "=r"(r0)
        : "r"(r7)
        : "memory"
    );
    
    return r0;
}

static inline int32_t __syscall1(uint32_t num, uint32_t a0) {
    register uint32_t r7 __asm__("r7") = num;
    register uint32_t r0 __asm__("r0") = a0;
    
    __asm__ volatile (
        "svc #0"
        : "+r"(r0)
        : "r"(r7)
        : "memory"
    );
    
    return (int32_t)r0;
}

static inline int32_t __syscall2(uint32_t num, uint32_t a0, uint32_t a1) {
    register uint32_t r7 __asm__("r7") = num;
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    
    __asm__ volatile (
        "svc #0"
        : "+r"(r0)
        : "r"(r7), "r"(r1)
        : "memory"
    );
    
    return (int32_t)r0;
}

static inline int32_t __syscall3(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2) {
    register uint32_t r7 __asm__("r7") = num;
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    
    __asm__ volatile (
        "svc #0"
        : "+r"(r0)
        : "r"(r7), "r"(r1), "r"(r2)
        : "memory"
    );
    
    return (int32_t)r0;
}

// ============================================================================
// SYSCALL WRAPPERS
// ============================================================================

// Process control
static inline void mimi_exit(int code) {
    __syscall1(SYS_EXIT, code);
}

static inline void mimi_yield(void) {
    __syscall0(SYS_YIELD);
}

static inline void mimi_sleep(uint32_t ms) {
    __syscall1(SYS_SLEEP, ms);
}

static inline uint32_t mimi_time(void) {
    return __syscall0(SYS_TIME);
}

// Memory
static inline void* mimi_malloc(size_t size) {
    return (void*)__syscall1(SYS_MALLOC, size);
}

static inline void mimi_free(void* ptr) {
    __syscall1(SYS_FREE, (uint32_t)ptr);
}

static inline void* mimi_realloc(void* ptr, size_t size) {
    return (void*)__syscall2(SYS_REALLOC, (uint32_t)ptr, size);
}

// File I/O
static inline int mimi_open(const char* path, int flags) {
    return __syscall2(SYS_OPEN, (uint32_t)path, flags);
}

static inline int mimi_close(int fd) {
    return __syscall1(SYS_CLOSE, fd);
}

static inline int mimi_read(int fd, void* buf, size_t size) {
    return __syscall3(SYS_READ, fd, (uint32_t)buf, size);
}

static inline int mimi_write(int fd, const void* buf, size_t size) {
    return __syscall3(SYS_WRITE, fd, (uint32_t)buf, size);
}

static inline int mimi_seek(int fd, int offset, int whence) {
    return __syscall3(SYS_SEEK, fd, offset, whence);
}

// Console
static inline int mimi_putchar(int c) {
    return __syscall1(SYS_PUTCHAR, c);
}

static inline int mimi_getchar(void) {
    return __syscall0(SYS_GETCHAR);
}

static inline int mimi_puts(const char* s) {
    return __syscall1(SYS_PUTS, (uint32_t)s);
}

// GPIO
static inline void mimi_gpio_init(uint32_t pin) {
    __syscall1(SYS_GPIO_INIT, pin);
}

static inline void mimi_gpio_set_dir(uint32_t pin, bool out) {
    __syscall2(SYS_GPIO_DIR, pin, out);
}

static inline void mimi_gpio_put(uint32_t pin, bool value) {
    __syscall2(SYS_GPIO_PUT, pin, value);
}

static inline bool mimi_gpio_get(uint32_t pin) {
    return __syscall1(SYS_GPIO_GET, pin);
}

static inline void mimi_gpio_set_pulls(uint32_t pin, bool up, bool down) {
    uint32_t mode = up ? 1 : (down ? 2 : 0);
    __syscall2(SYS_GPIO_PULL, pin, mode);
}

// PWM
static inline void mimi_pwm_init(uint32_t slice) {
    __syscall1(SYS_PWM_INIT, slice);
}

static inline void mimi_pwm_set_wrap(uint32_t slice, uint16_t wrap) {
    __syscall2(SYS_PWM_SET_WRAP, slice, wrap);
}

static inline void mimi_pwm_set_level(uint32_t slice, uint32_t channel, uint16_t level) {
    __syscall3(SYS_PWM_SET_LEVEL, slice, channel, level);
}

static inline void mimi_pwm_enable(uint32_t slice, bool enable) {
    __syscall2(SYS_PWM_ENABLE, slice, enable);
}

// ADC
static inline void mimi_adc_init(void) {
    __syscall0(SYS_ADC_INIT);
}

static inline void mimi_adc_select(uint32_t channel) {
    __syscall1(SYS_ADC_SELECT, channel);
}

static inline uint16_t mimi_adc_read(void) {
    return __syscall0(SYS_ADC_READ);
}

static inline float mimi_adc_read_temp(void) {
    uint32_t raw = __syscall0(SYS_ADC_TEMP);
    // Convert raw ADC to temperature
    return 27.0f - ((raw * 3.3f / 4096.0f) - 0.706f) / 0.001721f;
}

// SPI
static inline void mimi_spi_init(uint32_t spi, uint32_t baudrate) {
    __syscall2(SYS_SPI_INIT, spi, baudrate);
}

static inline int mimi_spi_write(uint32_t spi, const uint8_t* data, size_t len) {
    return __syscall3(SYS_SPI_WRITE, spi, (uint32_t)data, len);
}

static inline int mimi_spi_read(uint32_t spi, uint8_t* data, size_t len) {
    return __syscall3(SYS_SPI_READ, spi, (uint32_t)data, len);
}

// I2C
static inline void mimi_i2c_init(uint32_t i2c, uint32_t baudrate) {
    __syscall2(SYS_I2C_INIT, i2c, baudrate);
}

static inline int mimi_i2c_write(uint32_t i2c, uint8_t addr, const uint8_t* data, size_t len) {
    // Pack addr and i2c into one arg
    return __syscall3(SYS_I2C_WRITE, (i2c << 8) | addr, (uint32_t)data, len);
}

static inline int mimi_i2c_read(uint32_t i2c, uint8_t addr, uint8_t* data, size_t len) {
    return __syscall3(SYS_I2C_READ, (i2c << 8) | addr, (uint32_t)data, len);
}

#endif // MIMIC_SYSCALL_H

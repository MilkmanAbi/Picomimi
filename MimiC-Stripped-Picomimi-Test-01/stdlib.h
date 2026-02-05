/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC SDK - pico-sdk Compatible API                                      ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  User code looks like pico-sdk, but calls MimiC syscalls underneath       ║
 * ║  This header is used by compiled user programs                            ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 * 
 * Example user code:
 * 
 *   #include <pico/stdlib.h>
 *   #include <hardware/gpio.h>
 *   
 *   int main() {
 *       stdio_init_all();
 *       gpio_init(25);
 *       gpio_set_dir(25, GPIO_OUT);
 *       
 *       while (1) {
 *           gpio_put(25, 1);
 *           sleep_ms(500);
 *           gpio_put(25, 0);
 *           sleep_ms(500);
 *       }
 *   }
 * 
 * This code compiles and runs on MimiC - the pico-sdk API is translated
 * to syscalls that the kernel handles.
 */

#ifndef PICO_STDLIB_H
#define PICO_STDLIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "mimic/syscall.h"

// ============================================================================
// BASIC TYPES
// ============================================================================

typedef unsigned int uint;

// ============================================================================
// STDIO
// ============================================================================

static inline void stdio_init_all(void) {
    // No-op in MimiC - kernel handles stdio
}

static inline int putchar(int c) {
    return mimi_putchar(c);
}

static inline int getchar(void) {
    return mimi_getchar();
}

static inline int puts(const char* s) {
    return mimi_puts(s);
}

// Simple printf implementation for integers and strings
int printf(const char* fmt, ...);

// ============================================================================
// TIMING
// ============================================================================

static inline void sleep_ms(uint32_t ms) {
    mimi_sleep(ms);
}

static inline void sleep_us(uint64_t us) {
    mimi_sleep((us + 999) / 1000);  // Convert to ms, round up
}

static inline uint32_t time_us_32(void) {
    return mimi_time() * 1000;  // Convert ms to us
}

static inline uint64_t time_us_64(void) {
    return (uint64_t)mimi_time() * 1000;
}

static inline void busy_wait_us(uint32_t us) {
    uint32_t start = time_us_32();
    while (time_us_32() - start < us) {
        // Busy wait
    }
}

// ============================================================================
// MEMORY
// ============================================================================

static inline void* malloc(size_t size) {
    return mimi_malloc(size);
}

static inline void free(void* ptr) {
    mimi_free(ptr);
}

static inline void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = mimi_malloc(total);
    if (ptr) {
        // Zero the memory
        uint8_t* p = (uint8_t*)ptr;
        for (size_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

static inline void* realloc(void* ptr, size_t size) {
    return mimi_realloc(ptr, size);
}

// ============================================================================
// STRING FUNCTIONS (minimal implementation)
// ============================================================================

static inline size_t strlen(const char* s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static inline char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

static inline char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (n-- && (*d++ = *src++));
    while (n--) *d++ = '\0';
    return dest;
}

static inline int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static inline void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static inline void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

#endif // PICO_STDLIB_H

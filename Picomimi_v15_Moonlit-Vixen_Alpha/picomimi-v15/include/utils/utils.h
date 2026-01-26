/**
 * PICOMIMI Utilities Header
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Includes:
 * - Ring buffer (lock-free SPSC)
 * - Circular buffer
 * - String utilities
 * - Math helpers
 * - Bit manipulation
 * - CRC calculations
 * - Time utilities
 */
#ifndef PICOMIMI_UTILS_H
#define PICOMIMI_UTILS_H

#include "config/picomimi_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// RING BUFFER (Lock-free SPSC for single producer, single consumer)
// ============================================================================

#define PM_RINGBUF_SIZE     256

typedef struct {
    uint8_t buffer[PM_RINGBUF_SIZE];
    volatile uint32_t head;  // Write position
    volatile uint32_t tail;  // Read position
} pm_ringbuf_t;

/**
 * Initialize a ring buffer
 */
void pm_ringbuf_init(pm_ringbuf_t* rb);

/**
 * Put a byte into the ring buffer
 * @return true if successful, false if full
 */
bool pm_ringbuf_put(pm_ringbuf_t* rb, uint8_t data);

/**
 * Get a byte from the ring buffer
 * @return true if successful, false if empty
 */
bool pm_ringbuf_get(pm_ringbuf_t* rb, uint8_t* data);

/**
 * Peek at the next byte without removing
 */
bool pm_ringbuf_peek(pm_ringbuf_t* rb, uint8_t* data);

/**
 * Check if ring buffer is empty
 */
bool pm_ringbuf_empty(pm_ringbuf_t* rb);

/**
 * Check if ring buffer is full
 */
bool pm_ringbuf_full(pm_ringbuf_t* rb);

/**
 * Get number of bytes available in ring buffer
 */
uint32_t pm_ringbuf_available(pm_ringbuf_t* rb);

/**
 * Get free space in ring buffer
 */
uint32_t pm_ringbuf_free(pm_ringbuf_t* rb);

/**
 * Clear the ring buffer
 */
void pm_ringbuf_clear(pm_ringbuf_t* rb);

// ============================================================================
// CIRCULAR BUFFER (Generic, variable size)
// ============================================================================

typedef struct {
    uint8_t* buffer;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} pm_circbuf_t;

/**
 * Initialize a circular buffer with external storage
 */
void pm_circbuf_init(pm_circbuf_t* cb, uint8_t* buffer, uint32_t size);

/**
 * Write data to circular buffer
 * @return Number of bytes written
 */
uint32_t pm_circbuf_write(pm_circbuf_t* cb, const uint8_t* data, uint32_t len);

/**
 * Read data from circular buffer
 * @return Number of bytes read
 */
uint32_t pm_circbuf_read(pm_circbuf_t* cb, uint8_t* data, uint32_t len);

/**
 * Get available data count
 */
uint32_t pm_circbuf_available(pm_circbuf_t* cb);

/**
 * Get free space
 */
uint32_t pm_circbuf_free(pm_circbuf_t* cb);

/**
 * Clear the buffer
 */
void pm_circbuf_clear(pm_circbuf_t* cb);

// ============================================================================
// STRING UTILITIES
// ============================================================================

/**
 * Safe string copy with length limit
 */
size_t pm_strlcpy(char* dst, const char* src, size_t size);

/**
 * Safe string concatenation with length limit
 */
size_t pm_strlcat(char* dst, const char* src, size_t size);

/**
 * Convert integer to string
 * @return Pointer to result string
 */
char* pm_itoa(int32_t value, char* str, int base);

/**
 * Convert unsigned integer to string
 */
char* pm_utoa(uint32_t value, char* str, int base);

/**
 * Convert string to integer
 */
int32_t pm_atoi(const char* str);

/**
 * Convert string to unsigned integer
 */
uint32_t pm_atou(const char* str);

/**
 * Convert hex string to integer
 */
uint32_t pm_htoi(const char* str);

/**
 * Format bytes to human readable (e.g., "1.5 KB")
 */
void pm_format_bytes(uint32_t bytes, char* buf, size_t buf_size);

/**
 * Format duration to human readable (e.g., "1h 23m 45s")
 */
void pm_format_duration(uint32_t seconds, char* buf, size_t buf_size);

/**
 * Trim whitespace from string (in-place)
 */
char* pm_strtrim(char* str);

/**
 * Split string by delimiter
 * @return Number of tokens found
 */
int pm_strsplit(char* str, char delimiter, char** tokens, int max_tokens);

/**
 * Case-insensitive string comparison
 */
int pm_strcasecmp(const char* s1, const char* s2);

/**
 * Check if string starts with prefix
 */
bool pm_startswith(const char* str, const char* prefix);

/**
 * Check if string ends with suffix
 */
bool pm_endswith(const char* str, const char* suffix);

// ============================================================================
// MATH HELPERS
// ============================================================================

/**
 * Clamp value between min and max
 */
static inline int32_t pm_clamp(int32_t value, int32_t min, int32_t max) {
    return (value < min) ? min : ((value > max) ? max : value);
}

static inline uint32_t pm_clampu(uint32_t value, uint32_t min, uint32_t max) {
    return (value < min) ? min : ((value > max) ? max : value);
}

static inline float pm_clampf(float value, float min, float max) {
    return (value < min) ? min : ((value > max) ? max : value);
}

/**
 * Map value from one range to another
 */
static inline int32_t pm_map(int32_t value, int32_t in_min, int32_t in_max,
                             int32_t out_min, int32_t out_max) {
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * Linear interpolation
 */
static inline float pm_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/**
 * Absolute value
 */
static inline int32_t pm_abs(int32_t x) {
    return (x < 0) ? -x : x;
}

/**
 * Min/max
 */
static inline int32_t pm_min(int32_t a, int32_t b) {
    return (a < b) ? a : b;
}

static inline int32_t pm_max(int32_t a, int32_t b) {
    return (a > b) ? a : b;
}

static inline uint32_t pm_minu(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static inline uint32_t pm_maxu(uint32_t a, uint32_t b) {
    return (a > b) ? a : b;
}

/**
 * Power of 2 checks
 */
static inline bool pm_is_power_of_2(uint32_t x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}

/**
 * Next power of 2
 */
static inline uint32_t pm_next_power_of_2(uint32_t x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

/**
 * Align value to alignment
 */
static inline uint32_t pm_align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static inline uint32_t pm_align_down(uint32_t value, uint32_t alignment) {
    return value & ~(alignment - 1);
}

// ============================================================================
// BIT MANIPULATION
// ============================================================================

/**
 * Set a bit
 */
static inline uint32_t pm_bit_set(uint32_t value, uint8_t bit) {
    return value | (1U << bit);
}

/**
 * Clear a bit
 */
static inline uint32_t pm_bit_clear(uint32_t value, uint8_t bit) {
    return value & ~(1U << bit);
}

/**
 * Toggle a bit
 */
static inline uint32_t pm_bit_toggle(uint32_t value, uint8_t bit) {
    return value ^ (1U << bit);
}

/**
 * Test a bit
 */
static inline bool pm_bit_test(uint32_t value, uint8_t bit) {
    return (value & (1U << bit)) != 0;
}

/**
 * Count leading zeros
 */
static inline uint32_t pm_clz(uint32_t value) {
    return (value == 0) ? 32 : __builtin_clz(value);
}

/**
 * Count trailing zeros
 */
static inline uint32_t pm_ctz(uint32_t value) {
    return (value == 0) ? 32 : __builtin_ctz(value);
}

/**
 * Population count (count set bits)
 */
static inline uint32_t pm_popcount(uint32_t value) {
    return __builtin_popcount(value);
}

/**
 * Find first set bit (1-indexed, 0 if none)
 */
static inline uint32_t pm_ffs(uint32_t value) {
    return __builtin_ffs(value);
}

/**
 * Find last set bit (1-indexed, 0 if none)
 */
static inline uint32_t pm_fls(uint32_t value) {
    return (value == 0) ? 0 : (32 - __builtin_clz(value));
}

/**
 * Byte swap
 */
static inline uint16_t pm_bswap16(uint16_t value) {
    return __builtin_bswap16(value);
}

static inline uint32_t pm_bswap32(uint32_t value) {
    return __builtin_bswap32(value);
}

// ============================================================================
// CRC CALCULATIONS
// ============================================================================

/**
 * CRC-8 (CCITT)
 */
uint8_t pm_crc8(const uint8_t* data, size_t len);

/**
 * CRC-16 (CCITT)
 */
uint16_t pm_crc16(const uint8_t* data, size_t len);

/**
 * CRC-32 (IEEE 802.3)
 */
uint32_t pm_crc32(const uint8_t* data, size_t len);

/**
 * Simple checksum
 */
uint8_t pm_checksum(const uint8_t* data, size_t len);

// ============================================================================
// TIME UTILITIES
// ============================================================================

/**
 * Get current time in microseconds
 */
uint64_t pm_time_us(void);

/**
 * Get current time in milliseconds
 */
uint32_t pm_time_ms(void);

/**
 * Delay in microseconds (blocking)
 */
void pm_delay_us(uint32_t us);

/**
 * Delay in milliseconds (blocking)
 */
void pm_delay_ms(uint32_t ms);

/**
 * Check if timeout elapsed
 */
bool pm_timeout_elapsed(uint32_t start_ms, uint32_t timeout_ms);

/**
 * Get elapsed time since start
 */
uint32_t pm_elapsed_ms(uint32_t start_ms);

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

/**
 * Seed the PRNG
 */
void pm_srand(uint32_t seed);

/**
 * Get a pseudo-random number
 */
uint32_t pm_rand(void);

/**
 * Get a random number in range [min, max]
 */
uint32_t pm_rand_range(uint32_t min, uint32_t max);

// ============================================================================
// ASSERT MACROS
// ============================================================================

#if PICOMIMI_DEBUG
    #define PM_ASSERT(cond) do { \
        if (!(cond)) { \
            pm_kprintf("ASSERT FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
            pm_kernel_panic("Assertion failed"); \
        } \
    } while(0)
#else
    #define PM_ASSERT(cond) ((void)0)
#endif

#define PM_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

// ============================================================================
// DEBUG MACROS
// ============================================================================

#if PICOMIMI_DEBUG
    #define PM_DEBUG(fmt, ...) pm_kprintf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
    #define PM_TRACE(fmt, ...) pm_kprintf("[TRACE] %s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
    #define PM_DEBUG(fmt, ...) ((void)0)
    #define PM_TRACE(fmt, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_UTILS_H

/**
 * PICOMIMI Utilities Implementation
 * Ported from v14.3.1 "Quiet Otter"
 */

#include "utils/utils.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <string.h>
#include <ctype.h>

// ============================================================================
// RING BUFFER
// ============================================================================

void pm_ringbuf_init(pm_ringbuf_t* rb) {
    rb->head = 0;
    rb->tail = 0;
    memset(rb->buffer, 0, PM_RINGBUF_SIZE);
}

bool pm_ringbuf_put(pm_ringbuf_t* rb, uint8_t data) {
    uint32_t next_head = (rb->head + 1) % PM_RINGBUF_SIZE;
    
    if (next_head == rb->tail) {
        return false;  // Full
    }
    
    rb->buffer[rb->head] = data;
    rb->head = next_head;
    return true;
}

bool pm_ringbuf_get(pm_ringbuf_t* rb, uint8_t* data) {
    if (rb->head == rb->tail) {
        return false;  // Empty
    }
    
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % PM_RINGBUF_SIZE;
    return true;
}

bool pm_ringbuf_peek(pm_ringbuf_t* rb, uint8_t* data) {
    if (rb->head == rb->tail) {
        return false;  // Empty
    }
    
    *data = rb->buffer[rb->tail];
    return true;
}

bool pm_ringbuf_empty(pm_ringbuf_t* rb) {
    return rb->head == rb->tail;
}

bool pm_ringbuf_full(pm_ringbuf_t* rb) {
    return ((rb->head + 1) % PM_RINGBUF_SIZE) == rb->tail;
}

uint32_t pm_ringbuf_available(pm_ringbuf_t* rb) {
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    } else {
        return PM_RINGBUF_SIZE - rb->tail + rb->head;
    }
}

uint32_t pm_ringbuf_free(pm_ringbuf_t* rb) {
    return PM_RINGBUF_SIZE - 1 - pm_ringbuf_available(rb);
}

void pm_ringbuf_clear(pm_ringbuf_t* rb) {
    rb->head = 0;
    rb->tail = 0;
}

// ============================================================================
// CIRCULAR BUFFER
// ============================================================================

void pm_circbuf_init(pm_circbuf_t* cb, uint8_t* buffer, uint32_t size) {
    cb->buffer = buffer;
    cb->size = size;
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
}

uint32_t pm_circbuf_write(pm_circbuf_t* cb, const uint8_t* data, uint32_t len) {
    uint32_t written = 0;
    
    while (written < len && cb->count < cb->size) {
        cb->buffer[cb->head] = data[written];
        cb->head = (cb->head + 1) % cb->size;
        cb->count++;
        written++;
    }
    
    return written;
}

uint32_t pm_circbuf_read(pm_circbuf_t* cb, uint8_t* data, uint32_t len) {
    uint32_t read = 0;
    
    while (read < len && cb->count > 0) {
        data[read] = cb->buffer[cb->tail];
        cb->tail = (cb->tail + 1) % cb->size;
        cb->count--;
        read++;
    }
    
    return read;
}

uint32_t pm_circbuf_available(pm_circbuf_t* cb) {
    return cb->count;
}

uint32_t pm_circbuf_free(pm_circbuf_t* cb) {
    return cb->size - cb->count;
}

void pm_circbuf_clear(pm_circbuf_t* cb) {
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
}

// ============================================================================
// STRING UTILITIES
// ============================================================================

size_t pm_strlcpy(char* dst, const char* src, size_t size) {
    size_t src_len = strlen(src);
    
    if (size > 0) {
        size_t copy_len = (src_len >= size) ? size - 1 : src_len;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    
    return src_len;
}

size_t pm_strlcat(char* dst, const char* src, size_t size) {
    size_t dst_len = strlen(dst);
    size_t src_len = strlen(src);
    
    if (dst_len >= size) {
        return size + src_len;
    }
    
    size_t remaining = size - dst_len - 1;
    size_t copy_len = (src_len <= remaining) ? src_len : remaining;
    
    memcpy(dst + dst_len, src, copy_len);
    dst[dst_len + copy_len] = '\0';
    
    return dst_len + src_len;
}

char* pm_itoa(int32_t value, char* str, int base) {
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    int32_t tmp_value;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    // Handle negative numbers for base 10
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        ptr1 = ptr;
        value = -value;
    }
    
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value - value * base];
    } while (value);
    
    *ptr-- = '\0';
    
    // Reverse
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    
    return str;
}

char* pm_utoa(uint32_t value, char* str, int base) {
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    uint32_t tmp_value;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value - value * base];
    } while (value);
    
    *ptr-- = '\0';
    
    // Reverse
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    
    return str;
}

int32_t pm_atoi(const char* str) {
    int32_t result = 0;
    int sign = 1;
    
    while (*str == ' ') str++;
    
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

uint32_t pm_atou(const char* str) {
    uint32_t result = 0;
    
    while (*str == ' ') str++;
    if (*str == '+') str++;
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return result;
}

uint32_t pm_htoi(const char* str) {
    uint32_t result = 0;
    
    while (*str == ' ') str++;
    
    // Skip 0x prefix
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }
    
    while (*str) {
        char c = *str;
        uint32_t digit;
        
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = 10 + (c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            digit = 10 + (c - 'A');
        } else {
            break;
        }
        
        result = (result << 4) | digit;
        str++;
    }
    
    return result;
}

void pm_format_bytes(uint32_t bytes, char* buf, size_t buf_size) {
    if (bytes < 1024) {
        snprintf(buf, buf_size, "%lu B", (unsigned long)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, buf_size, "%.1f KB", bytes / 1024.0f);
    } else {
        snprintf(buf, buf_size, "%.1f MB", bytes / (1024.0f * 1024.0f));
    }
}

void pm_format_duration(uint32_t seconds, char* buf, size_t buf_size) {
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;
    
    if (days > 0) {
        snprintf(buf, buf_size, "%lud %luh %lum", 
                (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
    } else if (hours > 0) {
        snprintf(buf, buf_size, "%luh %lum %lus", 
                (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    } else if (mins > 0) {
        snprintf(buf, buf_size, "%lum %lus", (unsigned long)mins, (unsigned long)secs);
    } else {
        snprintf(buf, buf_size, "%lus", (unsigned long)secs);
    }
}

char* pm_strtrim(char* str) {
    char* end;
    
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    
    return str;
}

int pm_strsplit(char* str, char delimiter, char** tokens, int max_tokens) {
    int count = 0;
    char* token = str;
    
    while (*str && count < max_tokens) {
        if (*str == delimiter) {
            *str = '\0';
            tokens[count++] = token;
            token = str + 1;
        }
        str++;
    }
    
    if (count < max_tokens && *token) {
        tokens[count++] = token;
    }
    
    return count;
}

int pm_strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        
        if (c1 != c2) return c1 - c2;
        
        s1++;
        s2++;
    }
    
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

bool pm_startswith(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return false;
    }
    return true;
}

bool pm_endswith(const char* str, const char* suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) return false;
    
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

// ============================================================================
// CRC CALCULATIONS
// ============================================================================

uint8_t pm_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    
    while (len--) {
        crc ^= *data++;
        
        for (int i = 0; i < 8; i++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;  // Polynomial
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

uint16_t pm_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    
    while (len--) {
        crc ^= *data++;
        
        for (int i = 0; i < 8; i++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;  // Polynomial (reversed)
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

// CRC-32 table (IEEE 802.3)
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706B3,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t pm_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    
    while (len--) {
        crc = crc32_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }
    
    return ~crc;
}

uint8_t pm_checksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    
    while (len--) {
        sum += *data++;
    }
    
    return ~sum + 1;  // Two's complement
}

// ============================================================================
// TIME UTILITIES
// ============================================================================

uint64_t pm_time_us(void) {
    return to_us_since_boot(get_absolute_time());
}

uint32_t pm_time_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

void pm_delay_us(uint32_t us) {
    busy_wait_us(us);
}

void pm_delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

bool pm_timeout_elapsed(uint32_t start_ms, uint32_t timeout_ms) {
    return (pm_time_ms() - start_ms) >= timeout_ms;
}

uint32_t pm_elapsed_ms(uint32_t start_ms) {
    return pm_time_ms() - start_ms;
}

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

static uint32_t g_rand_seed = 1;

void pm_srand(uint32_t seed) {
    g_rand_seed = seed;
}

uint32_t pm_rand(void) {
    // xorshift32
    g_rand_seed ^= g_rand_seed << 13;
    g_rand_seed ^= g_rand_seed >> 17;
    g_rand_seed ^= g_rand_seed << 5;
    return g_rand_seed;
}

uint32_t pm_rand_range(uint32_t min, uint32_t max) {
    if (min >= max) return min;
    return min + (pm_rand() % (max - min + 1));
}

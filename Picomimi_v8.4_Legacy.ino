#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <SD.h>
#include <hardware/adc.h>
#include <hardware/watchdog.h>
#include <hardware/sync.h>
#include <hardware/flash.h>
#include <pico/platform.h>

#define disable_all_interrupts() __asm__ volatile ("cpsid i" : : : "memory")
#define enable_all_interrupts() __asm__ volatile ("cpsie i" : : : "memory")

#define LCD_CS      21
#define LCD_RESET   20
#define LCD_DC      17
#define LCD_MOSI    19
#define LCD_SCK     18
#define LCD_LED     22

#define SD_CS       5
#define SD_MOSI     19
#define SD_MISO     16
#define SD_SCK      18

#define BTN_LEFT    3
#define BTN_RIGHT   1
#define BTN_TOP     0
#define BTN_BOTTOM  2
#define BTN_SELECT  4
#define BTN_START   6
#define BTN_A       7
#define BTN_B       8
#define BTN_ONOFF   9

#define LCD_WIDTH   320
#define LCD_HEIGHT  240

#define MAX_TASKS 32
#define MAX_MEMORY_BLOCKS 256
#define HEAP_SIZE (180 * 1024)
#define TASK_NAME_LEN 24
#define MAX_LOG_ENTRIES 40
#define SCHEDULER_TICK_US 1000

#define VFS_BLOCK_SIZE 256
#define VFS_MAX_FILES 16
#define VFS_FILENAME_LEN 16
#define VFS_MAX_FILE_SIZE (16 * 1024)
#define VFS_STORAGE_SIZE (128 * 1024)
#define VFS_FLASH_OFFSET (1024 * 1024)

#define FS_MAX_FILENAME 32
#define FS_MAX_OPEN_FILES 8
#define FS_BUFFER_SIZE 512
#define FS_MAX_CARD_SIZE (4ULL * 1024 * 1024 * 1024)
#define FS_LOG_FILE "/LogRecord"

enum TaskState : uint8_t {
    TASK_READY,
    TASK_RUNNING,
    TASK_WAITING,
    TASK_SUSPENDED,
    TASK_TERMINATED,
    TASK_ZOMBIE
};

#define TASK_TYPE_KERNEL      0x01
#define TASK_TYPE_DRIVER      0x02
#define TASK_TYPE_SERVICE     0x04
#define TASK_TYPE_MODULE      0x08
#define TASK_TYPE_APPLICATION 0x10

#define TASK_FLAG_PROTECTED   0x01
#define TASK_FLAG_CRITICAL    0x02
#define TASK_FLAG_RESPAWN     0x04
#define TASK_FLAG_ONESHOT     0x08
#define TASK_FLAG_PERSISTENT  0x10

#define OOM_PRIORITY_NEVER      0
#define OOM_PRIORITY_CRITICAL   1
#define OOM_PRIORITY_HIGH       2
#define OOM_PRIORITY_NORMAL     3
#define OOM_PRIORITY_LOW        4

#define FILE_TYPE_TEXT    0x01
#define FILE_TYPE_LOG     0x02
#define FILE_TYPE_DATA    0x03
#define FILE_TYPE_CONFIG  0x04

struct ModuleCallbacks {
    void (*init)();
    void (*tick)(void*);
    void (*deinit)();
};

struct TCB {
    uint32_t id;
    TaskState state;
    uint8_t task_type;
    uint8_t oom_priority;
    uint8_t priority;
    uint8_t _padding;
    
    void (*entry)(void*);
    void* arg;
    ModuleCallbacks* callbacks;
    
    uint32_t flags;
    uint64_t wake_time;
    uint32_t mem_used;
    uint32_t mem_peak;
    
    char name[TASK_NAME_LEN];
    uint32_t mem_limit;
    uint64_t start_time;
    uint64_t max_runtime;
    uint64_t last_respawn;
    uint32_t respawn_count;
    uint32_t cpu_time;
    uint32_t last_run;
    uint32_t page_faults;
    uint32_t context_switches;
    const char* description;
} __attribute__((aligned(64)));

struct MemBlock {
    void* addr;
    uint32_t size;
    uint32_t owner_id;
    uint32_t alloc_seq;
    uint32_t alloc_time;
    bool free;
    uint8_t _padding[3];
} __attribute__((packed));

struct LogEntry {
    uint64_t timestamp;
    char message[56];
    uint8_t level;
    uint8_t _padding[7];
} __attribute__((aligned(8)));

struct VFSFile {
    char name[VFS_FILENAME_LEN];
    uint8_t type;
    bool in_use;
    uint16_t block_start;
    uint32_t size;
    uint32_t created;
    uint32_t modified;
    uint32_t owner_id;
} __attribute__((packed));

struct VFSSuperblock {
    uint32_t magic;
    uint32_t version;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t file_count;
    uint8_t block_bitmap[VFS_STORAGE_SIZE / VFS_BLOCK_SIZE / 8];
    VFSFile files[VFS_MAX_FILES];
} __attribute__((packed));

struct FSFile {
    File handle;
    char path[FS_MAX_FILENAME];
    bool open;
    bool write_mode;
};

struct KernelState {
    TCB tasks[MAX_TASKS];
    uint32_t task_count;
    uint32_t current_task;
    
    MemBlock mem_blocks[MAX_MEMORY_BLOCKS];
    uint32_t mem_block_count;
    
    uint64_t uptime_ms;
    bool running;
    bool panic_mode;
    
    uint32_t total_allocations;
    uint32_t total_frees;
    uint32_t oom_kills;
    uint32_t alloc_sequence;
    uint32_t fragmentation_pct;
    uint32_t largest_free_block;
    
    uint8_t kernel_tasks;
    uint8_t driver_tasks;
    uint8_t service_tasks;
    uint8_t module_tasks;
    uint8_t application_tasks;
    
    bool shell_alive;
    bool display_alive;
    bool input_alive;
    bool cpumon_alive;
    bool tempmon_alive;
    bool vfs_alive;
    bool fs_alive;
    bool root_mode;
    
    float cpu_usage;
    float temperature;
    uint32_t total_context_switches;
    
    LogEntry log[MAX_LOG_ENTRIES];
    uint32_t log_head;
    uint32_t log_count;
    
    VFSSuperblock* vfs_sb;
    uint8_t* vfs_data;
    bool vfs_mounted;
    bool vfs_active;
    uint32_t vfs_writes;
    uint32_t vfs_reads;
    
    bool fs_available;
    bool fs_mounted;
    uint64_t fs_total_bytes;
    uint64_t fs_used_bytes;
    uint32_t fs_reads;
    uint32_t fs_writes;
    uint32_t fs_log_counter;
    FSFile fs_open_files[FS_MAX_OPEN_FILES];
    
    uint8_t heap[HEAP_SIZE];
};

static KernelState kernel __attribute__((aligned(64)));

static char cmd_buffer[128];
static uint32_t cmd_pos = 0;

static Adafruit_ILI9341 tft = Adafruit_ILI9341(LCD_CS, LCD_DC, LCD_RESET);

static char last_tasks_str[16] = "";
static char last_uptime_str[32] = "";
static char last_mem_str[32] = "";
static char last_cpu_str[16] = "";
static char last_temp_str[16] = "";

void shell_prompt();
void brutal_task_kill(uint32_t id);
void klog(uint8_t level, const char* msg);
void task_sleep(uint32_t ms);
void task_yield();
void* kmalloc(size_t size, uint32_t task_id);
void kfree(void* ptr);
size_t get_free_memory();
size_t get_used_memory();
size_t get_task_memory(uint32_t task_id);
void mem_compact();
void calculate_fragmentation();
void oom_killer();

void vfs_init();
void vfs_format();
bool vfs_mount();
void vfs_unmount();
int vfs_create(const char* name, uint8_t type, uint32_t owner_id);
int vfs_write(int fd, const void* data, uint32_t size);
int vfs_read(int fd, void* buffer, uint32_t size);
void vfs_delete(int fd);
void vfs_list();
void vfs_stats();

void fs_init();
bool fs_mount();
void fs_unmount();
bool fs_exists(const char* path);
bool fs_mkdir(const char* path);
bool fs_remove(const char* path);
void fs_list(const char* path);
void fs_stats();
int fs_open(const char* path, bool write_mode);
void fs_close(int fd);
int fs_write_str(int fd, const char* data);
int fs_read_str(int fd, char* buffer, size_t size);
void fs_cat(const char* path);
void fs_log_init();
void fs_log_write(const char* message);

static inline uint64_t get_time_us() {
    return micros();
}

static inline uint64_t get_time_ms() {
    return millis();
}

static inline void precise_sleep_us(uint32_t us) {
    if (us == 0) return;
    delayMicroseconds(us);
}

static inline bool gpio_read_fast(uint8_t pin) {
    return digitalRead(pin) == LOW;
}

void klog(uint8_t level, const char* msg) {
    uint32_t irq_state = save_and_disable_interrupts();
    
    LogEntry* entry = &kernel.log[kernel.log_head];
    entry->timestamp = get_time_ms();
    entry->level = level;
    strncpy(entry->message, msg, sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';
    
    kernel.log_head = (kernel.log_head + 1) % MAX_LOG_ENTRIES;
    if (kernel.log_count < MAX_LOG_ENTRIES) {
        kernel.log_count++;
    }
    
    restore_interrupts(irq_state);
    
    if (kernel.fs_mounted && level >= 2) {
        fs_log_write(msg);
    }
}

void vfs_init() {
    kernel.vfs_sb = NULL;
    kernel.vfs_data = NULL;
    kernel.vfs_mounted = false;
    kernel.vfs_active = false;
    kernel.vfs_writes = 0;
    kernel.vfs_reads = 0;
    
    Serial.println("[VFS] Initialized (inactive)");
    klog(0, "VFS: Init OK");
}

void vfs_format() {
    if (!kernel.vfs_sb || !kernel.vfs_data) {
        Serial.println("[VFS] Not allocated");
        return;
    }
    
    Serial.println("[VFS] Formatting filesystem...");
    
    memset(kernel.vfs_sb, 0, sizeof(VFSSuperblock));
    memset(kernel.vfs_data, 0xFF, VFS_STORAGE_SIZE);
    
    kernel.vfs_sb->magic = 0x52503230;
    kernel.vfs_sb->version = 1;
    kernel.vfs_sb->total_blocks = VFS_STORAGE_SIZE / VFS_BLOCK_SIZE;
    kernel.vfs_sb->free_blocks = kernel.vfs_sb->total_blocks;
    kernel.vfs_sb->file_count = 0;
    
    memset(kernel.vfs_sb->block_bitmap, 0, sizeof(kernel.vfs_sb->block_bitmap));
    
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        kernel.vfs_sb->files[i].in_use = false;
        kernel.vfs_sb->files[i].name[0] = '\0';
    }
    
    Serial.println("[VFS] Format complete");
    klog(0, "VFS: Formatted");
}

bool vfs_mount() {
    if (!kernel.vfs_active) {
        Serial.println("[VFS] Not active. Use 'vfscreate' first");
        return false;
    }
    
    if (!kernel.vfs_sb || !kernel.vfs_data) {
        Serial.println("[VFS] Not allocated");
        return false;
    }
    
    vfs_format();
    
    kernel.vfs_mounted = true;
    kernel.vfs_alive = true;
    
    Serial.println("[VFS] Mounted");
    klog(0, "VFS: Mounted");
    
    return true;
}

void vfs_unmount() {
    if (!kernel.vfs_mounted) return;
    
    kernel.vfs_mounted = false;
    kernel.vfs_alive = false;
    
    Serial.println("[VFS] Unmounted");
    klog(0, "VFS: Unmounted");
}

int vfs_create(const char* name, uint8_t type, uint32_t owner_id) {
    if (!kernel.vfs_mounted) {
        Serial.println("[VFS] Not mounted");
        return -1;
    }
    
    if (strlen(name) >= VFS_FILENAME_LEN) {
        Serial.println("[VFS] Filename too long");
        return -1;
    }
    
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (kernel.vfs_sb->files[i].in_use && 
            strcmp(kernel.vfs_sb->files[i].name, name) == 0) {
            Serial.println("[VFS] File exists");
            return -1;
        }
    }
    
    int fd = -1;
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (!kernel.vfs_sb->files[i].in_use) {
            fd = i;
            break;
        }
    }
    
    if (fd < 0) {
        Serial.println("[VFS] No free file entries");
        return -1;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    strncpy(file->name, name, VFS_FILENAME_LEN - 1);
    file->name[VFS_FILENAME_LEN - 1] = '\0';
    file->type = type;
    file->in_use = true;
    file->block_start = 0xFFFF;
    file->size = 0;
    file->created = get_time_ms();
    file->modified = file->created;
    file->owner_id = owner_id;
    
    kernel.vfs_sb->file_count++;
    
    Serial.print("[VFS] Created: ");
    Serial.println(name);
    
    return fd;
}

int vfs_write(int fd, const void* data, uint32_t size) {
    if (!kernel.vfs_mounted || fd < 0 || fd >= VFS_MAX_FILES) {
        return -1;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    if (!file->in_use) return -1;
    
    if (size > VFS_MAX_FILE_SIZE) {
        size = VFS_MAX_FILE_SIZE;
    }
    
    uint32_t blocks_needed = (size + VFS_BLOCK_SIZE - 1) / VFS_BLOCK_SIZE;
    
    if (blocks_needed > kernel.vfs_sb->free_blocks) {
        Serial.println("[VFS] Insufficient space");
        return -1;
    }
    
    uint16_t start_block = 0xFFFF;
    for (uint32_t i = 0; i < kernel.vfs_sb->total_blocks; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        
        if (!(kernel.vfs_sb->block_bitmap[byte_idx] & (1 << bit_idx))) {
            if (start_block == 0xFFFF) {
                start_block = i;
            }
            
            kernel.vfs_sb->block_bitmap[byte_idx] |= (1 << bit_idx);
            kernel.vfs_sb->free_blocks--;
            
            if ((i - start_block + 1) >= blocks_needed) {
                break;
            }
        }
    }
    
    if (start_block == 0xFFFF) {
        Serial.println("[VFS] Block allocation failed");
        return -1;
    }
    
    uint32_t offset = start_block * VFS_BLOCK_SIZE;
    memcpy(kernel.vfs_data + offset, data, size);
    
    file->block_start = start_block;
    file->size = size;
    file->modified = get_time_ms();
    
    kernel.vfs_writes++;
    
    return size;
}

int vfs_read(int fd, void* buffer, uint32_t size) {
    if (!kernel.vfs_mounted || fd < 0 || fd >= VFS_MAX_FILES) {
        return -1;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    if (!file->in_use || file->block_start == 0xFFFF) {
        return -1;
    }
    
    uint32_t read_size = size < file->size ? size : file->size;
    uint32_t offset = file->block_start * VFS_BLOCK_SIZE;
    
    memcpy(buffer, kernel.vfs_data + offset, read_size);
    
    kernel.vfs_reads++;
    
    return read_size;
}

void vfs_delete(int fd) {
    if (!kernel.vfs_mounted || fd < 0 || fd >= VFS_MAX_FILES) {
        return;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    if (!file->in_use) return;
    
    if (file->block_start != 0xFFFF) {
        uint32_t blocks = (file->size + VFS_BLOCK_SIZE - 1) / VFS_BLOCK_SIZE;
        for (uint32_t i = 0; i < blocks; i++) {
            uint32_t block = file->block_start + i;
            uint32_t byte_idx = block / 8;
            uint32_t bit_idx = block % 8;
            kernel.vfs_sb->block_bitmap[byte_idx] &= ~(1 << bit_idx);
            kernel.vfs_sb->free_blocks++;
        }
    }
    
    file->in_use = false;
    kernel.vfs_sb->file_count--;
    
    Serial.print("[VFS] Deleted: ");
    Serial.println(file->name);
}

void vfs_list() {
    if (!kernel.vfs_mounted) {
        Serial.println("[VFS] Not mounted");
        return;
    }
    
    Serial.println("\n=== VFS Contents ===");
    Serial.println("ID  Name             Type   Size    Owner");
    Serial.println("--  ---------------  -----  ------  -----");
    
    const char* type_str[] = {"", "TEXT", "LOG", "DATA", "CONF"};
    
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        VFSFile* file = &kernel.vfs_sb->files[i];
        if (file->in_use) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%2d  %-15s  %-5s  %6d  %5d",
                     i, file->name, 
                     file->type < 5 ? type_str[file->type] : "?",
                     file->size, file->owner_id);
            Serial.println(buf);
        }
    }
    
    Serial.print("\nFiles: ");
    Serial.print(kernel.vfs_sb->file_count);
    Serial.print("/");
    Serial.println(VFS_MAX_FILES);
}

void vfs_stats() {
    if (!kernel.vfs_mounted) {
        Serial.println("[VFS] Not mounted");
        return;
    }
    
    Serial.println("\n=== VFS Statistics ===");
    Serial.print("Total blocks:  "); Serial.println(kernel.vfs_sb->total_blocks);
    Serial.print("Free blocks:   "); Serial.println(kernel.vfs_sb->free_blocks);
    Serial.print("Files:         "); Serial.print(kernel.vfs_sb->file_count);
    Serial.print("/"); Serial.println(VFS_MAX_FILES);
    Serial.print("Total writes:  "); Serial.println(kernel.vfs_writes);
    Serial.print("Total reads:   "); Serial.println(kernel.vfs_reads);
}

void fs_init() {
    kernel.fs_available = false;
    kernel.fs_mounted = false;
    kernel.fs_total_bytes = 0;
    kernel.fs_used_bytes = 0;
    kernel.fs_reads = 0;
    kernel.fs_writes = 0;
    kernel.fs_log_counter = 0;
    
    for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
        kernel.fs_open_files[i].open = false;
    }
    
    Serial.println("[FS] Initializing SD card...");
    
    SPI.setRX(SD_MISO);
    SPI.setTX(SD_MOSI);
    SPI.setSCK(SD_SCK);
    
    if (!SD.begin(SD_CS, 400000)) {
        Serial.println("[FS] SD card not detected");
        klog(1, "FS: No SD card");
        return;
    }
    
    Serial.println("[FS] SD card detected, increasing speed...");
    SD.end();
    delay(100);
    
    if (!SD.begin(SD_CS, 4000000)) {
        Serial.println("[FS] Failed to set 4MHz speed");
        klog(2, "FS: Speed init failed");
        return;
    }
    
    kernel.fs_available = true;
    
    Serial.println("[FS] Card detected");
    
    File root = SD.open("/");
    if (root) {
        kernel.fs_total_bytes = FS_MAX_CARD_SIZE;
        kernel.fs_used_bytes = 0;
        
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                kernel.fs_used_bytes += file.size();
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
        
        Serial.print("[FS] Estimated size: ");
        Serial.print(kernel.fs_total_bytes / (1024 * 1024));
        Serial.println(" MB (max 4GB)");
        
        Serial.print("[FS] Used: ");
        Serial.print(kernel.fs_used_bytes / (1024 * 1024));
        Serial.println(" MB");
    } else {
        kernel.fs_total_bytes = FS_MAX_CARD_SIZE;
        kernel.fs_used_bytes = 0;
        Serial.println("[FS] Size detection skipped");
    }
    
    klog(0, "FS: Init OK");
}

void fs_log_init() {
    if (!kernel.fs_available) return;
    
    File logFile = SD.open(FS_LOG_FILE, FILE_READ);
    if (!logFile) {
        Serial.println("[FS] Creating LogRecord file");
        logFile = SD.open(FS_LOG_FILE, FILE_WRITE);
        if (logFile) {
            logFile.println("=== RP2040 Kernel Error Log ===");
            logFile.println("Format: [N] Timestamp Message");
            logFile.println("================================");
            logFile.close();
            kernel.fs_log_counter = 0;
            Serial.println("[FS] LogRecord created");
        } else {
            Serial.println("[FS] Failed to create LogRecord");
            return;
        }
    } else {
        kernel.fs_log_counter = 0;
        while (logFile.available()) {
            String line = logFile.readStringUntil('\n');
            if (line.startsWith("[")) {
                int endBracket = line.indexOf(']');
                if (endBracket > 0) {
                    String numStr = line.substring(1, endBracket);
                    uint32_t num = numStr.toInt();
                    if (num > kernel.fs_log_counter) {
                        kernel.fs_log_counter = num;
                    }
                }
            }
        }
        logFile.close();
        
        Serial.print("[FS] LogRecord found, last entry: ");
        Serial.println(kernel.fs_log_counter);
    }
}

void fs_log_write(const char* message) {
    if (!kernel.fs_mounted) return;
    
    File logFile = SD.open(FS_LOG_FILE, FILE_WRITE);
    if (!logFile) {
        return;
    }
    
    kernel.fs_log_counter++;
    
    char timestamp[32];
    uint64_t ms = get_time_ms();
    snprintf(timestamp, sizeof(timestamp), "%lu.%03lu", 
             (uint32_t)(ms / 1000), (uint32_t)(ms % 1000));
    
    logFile.print("[");
    logFile.print(kernel.fs_log_counter);
    logFile.print("] ");
    logFile.print(timestamp);
    logFile.print(" ");
    logFile.println(message);
    
    logFile.close();
}

bool fs_mount() {
    if (!kernel.fs_available) {
        Serial.println("[FS] SD card unavailable");
        return false;
    }
    
    kernel.fs_mounted = true;
    kernel.fs_alive = true;
    
    fs_log_init();
    
    Serial.println("[FS] Mounted");
    klog(0, "FS: Mounted");
    
    return true;
}

void fs_unmount() {
    if (!kernel.fs_mounted) return;
    
    for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
        if (kernel.fs_open_files[i].open) {
            kernel.fs_open_files[i].handle.close();
            kernel.fs_open_files[i].open = false;
        }
    }
    
    kernel.fs_mounted = false;
    kernel.fs_alive = false;
    
    Serial.println("[FS] Unmounted");
    klog(0, "FS: Unmounted");
}

bool fs_exists(const char* path) {
    if (!kernel.fs_mounted) return false;
    return SD.exists(path);
}

bool fs_mkdir(const char* path) {
    if (!kernel.fs_mounted) return false;
    return SD.mkdir(path);
}

bool fs_remove(const char* path) {
    if (!kernel.fs_mounted) return false;
    return SD.remove(path);
}

void fs_list(const char* path) {
    if (!kernel.fs_mounted) {
        Serial.println("[FS] Not mounted");
        return;
    }
    
    File root = SD.open(path);
    if (!root) {
        Serial.println("[FS] Failed to open directory");
        return;
    }
    
    if (!root.isDirectory()) {
        Serial.println("[FS] Not a directory");
        root.close();
        return;
    }
    
    Serial.println("\n=== FS Contents ===");
    Serial.println("Name                             Type   Size");
    Serial.println("-------------------------------  -----  --------");
    
    File file = root.openNextFile();
    while (file) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%-31s  %-5s  %8d",
                 file.name(),
                 file.isDirectory() ? "DIR" : "FILE",
                 file.size());
        Serial.println(buf);
        file.close();
        file = root.openNextFile();
    }
    
    root.close();
}

void fs_stats() {
    if (!kernel.fs_mounted) {
        Serial.println("[FS] Not mounted");
        return;
    }
    
    uint64_t used = 0;
    File root = SD.open("/");
    if (root) {
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                used += file.size();
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
    }
    
    Serial.println("\n=== FS Statistics ===");
    Serial.print("Total space:   "); 
    Serial.print(kernel.fs_total_bytes / (1024 * 1024)); 
    Serial.println(" MB (est)");
    
    Serial.print("Used space:    "); 
    Serial.print(used / (1024 * 1024)); 
    Serial.println(" MB");
    
    Serial.print("Free space:    "); 
    Serial.print((kernel.fs_total_bytes - used) / (1024 * 1024)); 
    Serial.println(" MB (est)");
    
    Serial.print("Total reads:   "); Serial.println(kernel.fs_reads);
    Serial.print("Total writes:  "); Serial.println(kernel.fs_writes);
}

int fs_open(const char* path, bool write_mode) {
    if (!kernel.fs_mounted) return -1;
    
    int fd = -1;
    for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
        if (!kernel.fs_open_files[i].open) {
            fd = i;
            break;
        }
    }
    
    if (fd < 0) {
        Serial.println("[FS] No free file handles");
        return -1;
    }
    
    File file;
    if (write_mode) {
        file = SD.open(path, FILE_WRITE);
    } else {
        file = SD.open(path, FILE_READ);
    }
    
    if (!file) {
        Serial.println("[FS] Failed to open file");
        return -1;
    }
    
    kernel.fs_open_files[fd].handle = file;
    kernel.fs_open_files[fd].open = true;
    kernel.fs_open_files[fd].write_mode = write_mode;
    strncpy(kernel.fs_open_files[fd].path, path, FS_MAX_FILENAME - 1);
    kernel.fs_open_files[fd].path[FS_MAX_FILENAME - 1] = '\0';
    
    return fd;
}

void fs_close(int fd) {
    if (fd < 0 || fd >= FS_MAX_OPEN_FILES) return;
    if (!kernel.fs_open_files[fd].open) return;
    
    kernel.fs_open_files[fd].handle.close();
    kernel.fs_open_files[fd].open = false;
}

int fs_write_str(int fd, const char* data) {
    if (fd < 0 || fd >= FS_MAX_OPEN_FILES) return -1;
    if (!kernel.fs_open_files[fd].open) return -1;
    if (!kernel.fs_open_files[fd].write_mode) return -1;
    
    int written = kernel.fs_open_files[fd].handle.print(data);
    kernel.fs_writes++;
    return written;
}

int fs_read_str(int fd, char* buffer, size_t size) {
    if (fd < 0 || fd >= FS_MAX_OPEN_FILES) return -1;
    if (!kernel.fs_open_files[fd].open) return -1;
    
    int bytes = kernel.fs_open_files[fd].handle.readBytes(buffer, size);
    kernel.fs_reads++;
    return bytes;
}

void fs_cat(const char* path) {
    if (!kernel.fs_mounted) {
        Serial.println("[FS] Not mounted");
        return;
    }
    
    File file = SD.open(path, FILE_READ);
    if (!file) {
        Serial.println("[FS] Failed to open file");
        return;
    }
    
    Serial.println("\n=== File Contents ===");
    while (file.available()) {
        Serial.write(file.read());
    }
    Serial.println("\n=== End ===");
    
    file.close();
    kernel.fs_reads++;
}

void temp_init() {
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);
}

float read_temperature() {
    adc_select_input(4);
    uint16_t adc_raw = adc_read();
    const float conversion = 3.3f / 4096.0f;
    float voltage = adc_raw * conversion;
    float temp_c = 27.0f - (voltage - 0.706f) / 0.001721f;
    return temp_c;
}

void mem_init() {
    memset(&kernel.mem_blocks, 0, sizeof(kernel.mem_blocks));
    
    kernel.mem_block_count = 1;
    kernel.mem_blocks[0].addr = kernel.heap;
    kernel.mem_blocks[0].size = HEAP_SIZE;
    kernel.mem_blocks[0].owner_id = 0;
    kernel.mem_blocks[0].free = true;
    kernel.mem_blocks[0].alloc_time = 0;
    kernel.mem_blocks[0].alloc_seq = 0;
    
    kernel.total_allocations = 0;
    kernel.total_frees = 0;
    kernel.oom_kills = 0;
    kernel.alloc_sequence = 0;
    kernel.fragmentation_pct = 0;
    kernel.largest_free_block = HEAP_SIZE;
}

size_t get_free_memory() {
    size_t free_mem = 0;
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (kernel.mem_blocks[i].free) {
            free_mem += kernel.mem_blocks[i].size;
        }
    }
    return free_mem;
}

size_t get_used_memory() {
    return HEAP_SIZE - get_free_memory();
}

size_t get_task_memory(uint32_t task_id) {
    size_t mem = 0;
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (!kernel.mem_blocks[i].free && kernel.mem_blocks[i].owner_id == task_id) {
            mem += kernel.mem_blocks[i].size;
        }
    }
    return mem;
}

void calculate_fragmentation() {
    uint32_t free_blocks = 0;
    size_t largest = 0;
    size_t total_free = 0;
    
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (kernel.mem_blocks[i].free) {
            free_blocks++;
            total_free += kernel.mem_blocks[i].size;
            if (kernel.mem_blocks[i].size > largest) {
                largest = kernel.mem_blocks[i].size;
            }
        }
    }
    
    kernel.largest_free_block = largest;
    
    if (total_free > 0) {
        kernel.fragmentation_pct = 100 - ((largest * 100) / total_free);
    } else {
        kernel.fragmentation_pct = 0;
    }
}

void mem_compact() {
    bool merged;
    uint32_t merges = 0;
    
    do {
        merged = false;
        
        for (uint32_t i = 0; i < kernel.mem_block_count - 1; i++) {
            for (uint32_t j = i + 1; j < kernel.mem_block_count; j++) {
                if (kernel.mem_blocks[j].addr < kernel.mem_blocks[i].addr) {
                    MemBlock temp = kernel.mem_blocks[i];
                    kernel.mem_blocks[i] = kernel.mem_blocks[j];
                    kernel.mem_blocks[j] = temp;
                }
            }
        }
        
        for (uint32_t i = 0; i < kernel.mem_block_count - 1; i++) {
            if (!kernel.mem_blocks[i].free) continue;
            
            if (kernel.mem_blocks[i + 1].free &&
                (uint8_t*)kernel.mem_blocks[i].addr + kernel.mem_blocks[i].size == 
                kernel.mem_blocks[i + 1].addr) {
                
                kernel.mem_blocks[i].size += kernel.mem_blocks[i + 1].size;
                
                for (uint32_t k = i + 1; k < kernel.mem_block_count - 1; k++) {
                    kernel.mem_blocks[k] = kernel.mem_blocks[k + 1];
                }
                kernel.mem_block_count--;
                merged = true;
                merges++;
                break;
            }
        }
    } while (merged && merges < 50);
    
    if (merges > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "MEM: Compacted %d blocks", merges);
        klog(0, buf);
    }
}

void oom_killer() {
    Serial.println("\n!!! OUT OF MEMORY !!!");
    klog(3, "OOM: Out of memory!");
    
    Serial.println("OOM: Attempting memory compaction...");
    mem_compact();
    calculate_fragmentation();
    
    if (kernel.largest_free_block > 4096) {
        Serial.println("OOM: Compaction successful");
        klog(1, "OOM: Compaction resolved crisis");
        return;
    }
    
    Serial.println("OOM: Selecting APPLICATION victim...");
    
    uint32_t victim_id = 0;
    uint8_t highest_priority = 0;
    uint32_t max_mem = 0;
    
    for (uint32_t i = 1; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        
        if (task->task_type != TASK_TYPE_APPLICATION) continue;
        if (task->state == TASK_TERMINATED) continue;
        if (task->flags & TASK_FLAG_CRITICAL) continue;
        
        uint32_t task_mem = get_task_memory(task->id);
        
        if (task->oom_priority > highest_priority) {
            highest_priority = task->oom_priority;
            max_mem = task_mem;
            victim_id = task->id;
        } else if (task->oom_priority == highest_priority && task_mem > max_mem) {
            max_mem = task_mem;
            victim_id = task->id;
        }
    }
    
    if (victim_id > 0) {
        TCB* victim = &kernel.tasks[victim_id];
        Serial.print("OOM: Killing APPLICATION '");
        Serial.print(victim->name);
        Serial.print("' (");
        Serial.print(max_mem / 1024);
        Serial.println(" KB)");
        
        char buf[64];
        snprintf(buf, sizeof(buf), "OOM: Killed %s (%dKB)", victim->name, max_mem / 1024);
        klog(2, buf);
        
        brutal_task_kill(victim_id);
        kernel.oom_kills++;
    } else {
        Serial.println("OOM: NO KILLABLE APPLICATIONS!");
        Serial.println("*** SYSTEM PANIC ***");
        klog(3, "OOM: No victims, PANIC!");
        kernel.panic_mode = true;
    }
}

void* kmalloc(size_t size, uint32_t task_id) {
    if (size == 0) return NULL;
    
    size = (size + 3) & ~3;
    
    uint32_t irq_state = save_and_disable_interrupts();
    
    if (task_id < MAX_TASKS) {
        TCB* task = &kernel.tasks[task_id];
        task->page_faults++;
        
        if (task->task_type == TASK_TYPE_APPLICATION && task->mem_limit > 0) {
            uint32_t current_usage = get_task_memory(task_id);
            if (current_usage + size > task->mem_limit) {
                restore_interrupts(irq_state);
                return NULL;
            }
        }
    }
    
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        MemBlock* block = &kernel.mem_blocks[i];
        
        if (block->free && block->size >= size) {
            if (block->size > size + 32 && kernel.mem_block_count < MAX_MEMORY_BLOCKS) {
                MemBlock* new_block = &kernel.mem_blocks[kernel.mem_block_count++];
                new_block->addr = (uint8_t*)block->addr + size;
                new_block->size = block->size - size;
                new_block->owner_id = 0;
                new_block->free = true;
                new_block->alloc_time = 0;
                new_block->alloc_seq = 0;
                
                block->size = size;
            }
            
            block->free = false;
            block->owner_id = task_id;
            block->alloc_time = get_time_ms();
            block->alloc_seq = kernel.alloc_sequence++;
            kernel.total_allocations++;
            
            if (task_id < MAX_TASKS) {
                kernel.tasks[task_id].mem_used = get_task_memory(task_id);
                if (kernel.tasks[task_id].mem_used > kernel.tasks[task_id].mem_peak) {
                    kernel.tasks[task_id].mem_peak = kernel.tasks[task_id].mem_used;
                }
            }
            
            calculate_fragmentation();
            restore_interrupts(irq_state);
            return block->addr;
        }
    }
    
    restore_interrupts(irq_state);
    
    mem_compact();
    
    irq_state = save_and_disable_interrupts();
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        MemBlock* block = &kernel.mem_blocks[i];
        if (block->free && block->size >= size) {
            block->free = false;
            block->owner_id = task_id;
            block->alloc_time = get_time_ms();
            block->alloc_seq = kernel.alloc_sequence++;
            kernel.total_allocations++;
            
            if (task_id < MAX_TASKS) {
                kernel.tasks[task_id].mem_used = get_task_memory(task_id);
                if (kernel.tasks[task_id].mem_used > kernel.tasks[task_id].mem_peak) {
                    kernel.tasks[task_id].mem_peak = kernel.tasks[task_id].mem_used;
                }
            }
            
            calculate_fragmentation();
            restore_interrupts(irq_state);
            return block->addr;
        }
    }
    restore_interrupts(irq_state);
    
    oom_killer();
    
    irq_state = save_and_disable_interrupts();
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        MemBlock* block = &kernel.mem_blocks[i];
        if (block->free && block->size >= size) {
            block->free = false;
            block->owner_id = task_id;
            block->alloc_time = get_time_ms();
            block->alloc_seq = kernel.alloc_sequence++;
            kernel.total_allocations++;
            
            if (task_id < MAX_TASKS) {
                kernel.tasks[task_id].mem_used = get_task_memory(task_id);
                if (kernel.tasks[task_id].mem_used > kernel.tasks[task_id].mem_peak) {
                    kernel.tasks[task_id].mem_peak = kernel.tasks[task_id].mem_used;
                }
            }
            
            calculate_fragmentation();
            restore_interrupts(irq_state);
            return block->addr;
        }
    }
    restore_interrupts(irq_state);
    
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    uint32_t irq_state = save_and_disable_interrupts();
    
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (kernel.mem_blocks[i].addr == ptr) {
            uint32_t owner = kernel.mem_blocks[i].owner_id;
            kernel.mem_blocks[i].free = true;
            kernel.mem_blocks[i].owner_id = 0;
            kernel.total_frees++;
            
            if (owner < MAX_TASKS) {
                kernel.tasks[owner].mem_used = get_task_memory(owner);
            }
            
            calculate_fragmentation();
            restore_interrupts(irq_state);
            return;
        }
    }
    
    restore_interrupts(irq_state);
}

void task_init() {
    memset(&kernel.tasks, 0, sizeof(kernel.tasks));
    
    kernel.task_count = 0;
    kernel.current_task = 0;
    kernel.cpu_usage = 0.0f;
    kernel.root_mode = false;
    kernel.panic_mode = false;
    kernel.kernel_tasks = 0;
    kernel.driver_tasks = 0;
    kernel.service_tasks = 0;
    kernel.module_tasks = 0;
    kernel.application_tasks = 0;
    kernel.shell_alive = true;
    kernel.display_alive = true;
    kernel.input_alive = true;
    kernel.cpumon_alive = true;
    kernel.tempmon_alive = true;
    kernel.vfs_alive = false;
    kernel.fs_alive = false;
    kernel.log_head = 0;
    kernel.log_count = 0;
    kernel.total_context_switches = 0;
}

uint32_t task_create(const char* name, void (*entry)(void*), void* arg, 
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, ModuleCallbacks* callbacks,
                     const char* description) {
    
    if (kernel.task_count >= MAX_TASKS) {
        Serial.println("ERROR: Maximum tasks reached!");
        return 0;
    }
    
    if (task_type != TASK_TYPE_APPLICATION) {
        oom_priority = OOM_PRIORITY_NEVER;
    }
    
    if (flags & TASK_FLAG_ONESHOT) {
        for (uint32_t i = 0; i < kernel.task_count; i++) {
            if (strcmp(kernel.tasks[i].name, name) == 0) {
                if (kernel.tasks[i].state != TASK_TERMINATED) {
                    return 0;
                }
                uint64_t time_since_death = get_time_ms() - kernel.tasks[i].last_run;
                if (time_since_death < 2000) {
                    return 0;
                }
            }
        }
    }
    
    uint32_t id = kernel.task_count++;
    TCB* task = &kernel.tasks[id];
    
    memset(task, 0, sizeof(TCB));
    
    task->id = id;
    strncpy(task->name, name, TASK_NAME_LEN - 1);
    task->name[TASK_NAME_LEN - 1] = '\0';
    task->state = TASK_READY;
    task->task_type = task_type;
    task->priority = priority;
    task->flags = flags;
    task->oom_priority = oom_priority;
    task->entry = entry;
    task->arg = arg;
    task->wake_time = 0;
    task->start_time = get_time_ms();
    task->max_runtime = max_runtime_ms;
    task->last_respawn = 0;
    task->respawn_count = 0;
    task->mem_used = 0;
    task->mem_peak = 0;
    task->mem_limit = mem_limit;
    task->cpu_time = 0;
    task->last_run = 0;
    task->page_faults = 0;
    task->context_switches = 0;
    task->callbacks = callbacks;
    task->description = description;
    
    if (task_type == TASK_TYPE_KERNEL) kernel.kernel_tasks++;
    else if (task_type == TASK_TYPE_DRIVER) kernel.driver_tasks++;
    else if (task_type == TASK_TYPE_SERVICE) kernel.service_tasks++;
    else if (task_type == TASK_TYPE_MODULE) kernel.module_tasks++;
    else if (task_type == TASK_TYPE_APPLICATION) kernel.application_tasks++;
    
    if (callbacks && callbacks->init) {
        callbacks->init();
    }
    
    char buf[64];
    const char* type_str[] = {"KERN", "DRVR", "SRVC", "MODUL", "APP"};
    uint8_t type_idx = 0;
    if (task_type == TASK_TYPE_DRIVER) type_idx = 1;
    else if (task_type == TASK_TYPE_SERVICE) type_idx = 2;
    else if (task_type == TASK_TYPE_MODULE) type_idx = 3;
    else if (task_type == TASK_TYPE_APPLICATION) type_idx = 4;
    
    snprintf(buf, sizeof(buf), "TASK: %s [%s] ID=%d", name, type_str[type_idx], id);
    klog(0, buf);
    
    return id;
}

void task_sleep(uint32_t ms) {
    TCB* task = &kernel.tasks[kernel.current_task];
    task->state = TASK_WAITING;
    task->wake_time = get_time_ms() + ms;
}

void scheduler_tick() {
    uint64_t now = get_time_ms();
    kernel.uptime_ms = now;
    
    if (kernel.task_count == 0 || kernel.task_count > MAX_TASKS) {
        return;
    }
    
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        
        if (task->state == TASK_WAITING && now >= task->wake_time) {
            task->state = TASK_READY;
        }
        
        if (task->max_runtime > 0 && task->state != TASK_TERMINATED) {
            if ((now - task->start_time) > task->max_runtime) {
                char buf[64];
                snprintf(buf, sizeof(buf), "TIMEOUT: %s exceeded runtime", task->name);
                klog(1, buf);
                brutal_task_kill(task->id);
            }
        }
        
        if (task->state == TASK_TERMINATED && (task->flags & TASK_FLAG_RESPAWN)) {
            if (task->task_type == TASK_TYPE_KERNEL) {
                continue;
            }
            
            if (now - task->last_respawn > 5000) {
                Serial.print("[RESPAWN] '");
                Serial.print(task->name);
                Serial.print("' #");
                Serial.println(task->respawn_count + 1);
                
                task->state = TASK_READY;
                task->start_time = now;
                task->last_respawn = now;
                task->respawn_count++;
                task->mem_used = 0;
                task->cpu_time = 0;
                task->page_faults = 0;
                
                if (task->callbacks && task->callbacks->init) {
                    task->callbacks->init();
                }
                
                char buf[64];
                snprintf(buf, sizeof(buf), "RESPAWN: %s #%d", task->name, task->respawn_count);
                klog(0, buf);
            }
        }
    }
}

void task_yield() {
    uint32_t next = (kernel.current_task + 1) % kernel.task_count;
    uint32_t iterations = 0;
    
    while (iterations < kernel.task_count) {
        TCB* candidate = &kernel.tasks[next];
        
        if (candidate->state != TASK_TERMINATED && 
            (candidate->state == TASK_READY || candidate->state == TASK_RUNNING)) {
            kernel.current_task = next;
            kernel.total_context_switches++;
            candidate->context_switches++;
            return;
        }
        
        next = (next + 1) % kernel.task_count;
        iterations++;
    }
}

void brutal_task_kill(uint32_t id) {
    if (id >= kernel.task_count) return;
    
    TCB* task = &kernel.tasks[id];
    if (task->state == TASK_TERMINATED) return;
    
    Serial.print("[KILL] '");
    Serial.print(task->name);
    Serial.println("'");
    
    if (task->task_type == TASK_TYPE_KERNEL) {
        Serial.println("\n!!!!! KERNEL KILLED !!!!!");
        Serial.flush();
        
        kernel.running = false;
        kernel.task_count = 0;
        kernel.current_task = 0xFFFFFFFF;
        
        memset(&kernel.tasks, 0xFF, sizeof(kernel.tasks));
        memset(&kernel.mem_blocks, 0xFF, sizeof(kernel.mem_blocks));
        kernel.mem_block_count = 0;
        
        void (*crash)(void) = NULL;
        crash();
        
        while(1) { 
            __asm__("nop");
        }
    }
    
    if (task->callbacks && task->callbacks->deinit) {
        task->callbacks->deinit();
    }
    
    uint32_t freed = 0;
    for (uint32_t i = 0; i < kernel.mem_block_count; i++) {
        if (kernel.mem_blocks[i].owner_id == id) {
            freed += kernel.mem_blocks[i].size;
            kernel.mem_blocks[i].free = true;
            kernel.mem_blocks[i].owner_id = 0;
        }
    }
    
    if (freed > 0) {
        Serial.print("  > Freed ");
        Serial.print(freed);
        Serial.println(" bytes");
    }
    
    if (strcmp(task->name, "shell") == 0) {
        kernel.shell_alive = false;
        Serial.println("\n*** SHELL DEAD - NO MORE COMMANDS ***");
    }
    else if (strcmp(task->name, "display") == 0) {
        kernel.display_alive = false;
        Serial.println("\n*** DISPLAY DRIVER DEAD ***");
    }
    else if (strcmp(task->name, "input") == 0) {
        kernel.input_alive = false;
        Serial.println("\n*** INPUT DRIVER DEAD ***");
    }
    else if (strcmp(task->name, "cpumon") == 0) {
        kernel.cpumon_alive = false;
    }
    else if (strcmp(task->name, "tempmon") == 0) {
        kernel.tempmon_alive = false;
    }
    else if (strcmp(task->name, "vfs") == 0) {
        kernel.vfs_alive = false;
        Serial.println("\n*** VFS DEAD ***");
    }
    else if (strcmp(task->name, "fs") == 0) {
        kernel.fs_alive = false;
        Serial.println("\n*** FS DEAD ***");
    }
    
    if (task->task_type == TASK_TYPE_KERNEL) kernel.kernel_tasks--;
    else if (task->task_type == TASK_TYPE_DRIVER) kernel.driver_tasks--;
    else if (task->task_type == TASK_TYPE_SERVICE) kernel.service_tasks--;
    else if (task->task_type == TASK_TYPE_MODULE) kernel.module_tasks--;
    else if (task->task_type == TASK_TYPE_APPLICATION) kernel.application_tasks--;
    
    task->state = TASK_TERMINATED;
    task->mem_used = 0;
    task->last_run = get_time_ms();
    
    task->entry = NULL;
    task->callbacks = NULL;
    task->arg = NULL;
    
    char buf[64];
    snprintf(buf, sizeof(buf), "KILL: %s terminated", task->name);
    klog(2, buf);
}

void cmd_help() {
    Serial.println("\n=== System Commands ===");
    Serial.println("  help       - Show this help");
    Serial.println("  ps         - List all tasks");
    Serial.println("  listapps   - List only applications");
    Serial.println("  listmods   - List only modules");
    Serial.println("  top        - Live system monitor");
    Serial.println("  mem        - Memory statistics");
    Serial.println("  memmap     - Detailed memory map");
    Serial.println("  compact    - Force memory compaction");
    Serial.println("  dmesg      - Show system log");
    Serial.println("  uptime     - System uptime");
    Serial.println("  temp       - CPU temperature");
    Serial.println("  clear      - Clear screen");
    Serial.println("  reboot     - Restart system");
    Serial.println("  shutdown   - Safe shutdown");
    Serial.println("\n=== VFS Commands (RAM) ===");
    Serial.println("  vfscreate  - Create and mount VFS");
    Serial.println("  vfsls      - List VFS files");
    Serial.println("  vfsstat    - VFS statistics");
    Serial.println("  vfsmkfile <name> <type> - Create VFS file");
    Serial.println("  vfsrm <id>    - Delete VFS file");
    Serial.println("  vfscat <id>   - Read VFS file");
    Serial.println("  vfsdedicate   - Save all VFS files to SD");
    Serial.println("\n=== FS Commands (SD Card) ===");
    Serial.println("  ls [path]  - List SD files");
    Serial.println("  stat       - FS statistics");
    Serial.println("  mkdir <path> - Create directory");
    Serial.println("  rm <path>    - Delete file");
    Serial.println("  cat <path>   - Read file");
    Serial.println("  write <path> <text> - Write text to file");
    Serial.println("  logcat     - View error log from SD");
    Serial.println("\n=== Task Management ===");
    Serial.println("  kill <id>      - Kill task (apps only)");
    Serial.println("  root           - Toggle root mode");
    Serial.println("  root kill <id> - Force kill any task");
    Serial.println("  suspend <id>   - Suspend task");
    Serial.println("  resume <id>    - Resume task");
    Serial.println("\n=== Applications ===");
    Serial.println("  snake      - Snake game");
    Serial.println("  calc       - Calculator");
    Serial.println("  clock      - Digital clock");
    Serial.println("  sysmon     - System monitor");
    Serial.println("  memhog     - Memory stress test");
    Serial.println("  cpuburn    - CPU stress test");
    Serial.println("  stress     - Full system stress");
}

void cmd_arch() {
    Serial.println("\n=== RP2040 Kernel Task Architecture ===");
    Serial.println("\nTASK TYPES:");
    Serial.println("  1. KERNEL   - Core system (idle)");
    Serial.println("  2. DRIVER   - Hardware (display, input)");
    Serial.println("  3. SERVICE  - System services (shell, vfs, fs)");
    Serial.println("  4. MODULE   - Extensions (counter, watchdog)");
    Serial.println("  5. APPLICATION - User programs (games)");
    Serial.println("\nONLY APPLICATIONS can be OOM killed!");
}

void cmd_oom() {
    Serial.println("\n=== OOM Killer ===");
    Serial.println("ONLY kills APPLICATIONS");
    Serial.println("Priority: 0=Never 1=Crit 2=High 3=Norm 4=Low");
    Serial.println("Modules/Drivers/Services are PROTECTED");
}

void cmd_ps() {
    Serial.println("\nID  Name                 Type      State     Pri OOM  Mem(B)  Peak(B)");
    Serial.println("--- -------------------- --------- --------- --- ---  ------- --------");
    
    const char* state_str[] = {"READY", "RUN", "WAIT", "SUSP", "DEAD", "ZOMBI"};
    const char* type_str[] = {"KERNEL", "DRIVER", "SERVIC", "MODULE", "APP"};
    
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        
        uint8_t type_idx = 0;
        if (task->task_type == TASK_TYPE_DRIVER) type_idx = 1;
        else if (task->task_type == TASK_TYPE_SERVICE) type_idx = 2;
        else if (task->task_type == TASK_TYPE_MODULE) type_idx = 3;
        else if (task->task_type == TASK_TYPE_APPLICATION) type_idx = 4;
        
        char buf[128];
        snprintf(buf, sizeof(buf), "%2d  %-20s %-9s %-9s %3d %3d  %7d %8d",
                 task->id, task->name, type_str[type_idx], state_str[task->state],
                 task->priority, task->oom_priority, task->mem_used, task->mem_peak);
        Serial.println(buf);
    }
    
    Serial.print("\nSummary: K=");
    Serial.print(kernel.kernel_tasks);
    Serial.print(" D=");
    Serial.print(kernel.driver_tasks);
    Serial.print(" S=");
    Serial.print(kernel.service_tasks);
    Serial.print(" M=");
    Serial.print(kernel.module_tasks);
    Serial.print(" A=");
    Serial.println(kernel.application_tasks);
}

void cmd_listapps() {
    Serial.println("\n=== Running Applications ===");
    uint32_t count = 0;
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        if (task->task_type == TASK_TYPE_APPLICATION) {
            Serial.print(task->id);
            Serial.print(". ");
            Serial.print(task->name);
            Serial.print(" [OOM=");
            Serial.print(task->oom_priority);
            Serial.println("]");
            count++;
        }
    }
    if (count == 0) Serial.println("No applications running");
}

void cmd_listmods() {
    Serial.println("\n=== Loaded Modules ===");
    uint32_t count = 0;
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
        if (task->task_type == TASK_TYPE_MODULE) {
            Serial.print(task->id);
            Serial.print(". ");
            Serial.println(task->name);
            count++;
        }
    }
    if (count == 0) Serial.println("No modules loaded");
}

void cmd_mem() {
    uint32_t total = HEAP_SIZE;
    uint32_t used = get_used_memory();
    uint32_t free = get_free_memory();
    
    Serial.println("\n=== Memory Statistics ===");
    Serial.print("  Total:        "); Serial.print(total / 1024); Serial.println(" KB");
    Serial.print("  Used:         "); Serial.print(used / 1024); Serial.print(" KB (");
    Serial.print((used * 100) / total); Serial.println("%)");
    Serial.print("  Free:         "); Serial.print(free / 1024); Serial.println(" KB");
    Serial.print("  Largest free: "); Serial.print(kernel.largest_free_block / 1024); Serial.println(" KB");
    Serial.print("  Fragmentation:"); Serial.print(kernel.fragmentation_pct); Serial.println("%");
    Serial.print("  Total blocks: "); Serial.println(kernel.mem_block_count);
    Serial.print("  OOM kills:    "); Serial.println(kernel.oom_kills);
}

void cmd_memmap() {
    Serial.println("\n=== Memory Map (First 20 blocks) ===");
    uint32_t limit = kernel.mem_block_count < 20 ? kernel.mem_block_count : 20;
    
    for (uint32_t i = 0; i < limit; i++) {
        MemBlock* block = &kernel.mem_blocks[i];
        Serial.print(i);
        Serial.print(". ");
        Serial.print(block->free ? "FREE " : "USED ");
        Serial.print(block->size);
        Serial.print("B owner=");
        Serial.println(block->owner_id);
    }
}

void cmd_compact() {
    Serial.println("[COMPACT] Forcing memory compaction...");
    uint32_t before = kernel.mem_block_count;
    mem_compact();
    uint32_t after = kernel.mem_block_count;
    Serial.print("Blocks: ");
    Serial.print(before);
    Serial.print(" -> ");
    Serial.println(after);
}

void cmd_dmesg() {
    Serial.println("\n=== System Log ===");
    const char* level_str[] = {"INFO", "WARN", "ERR ", "CRIT"};
    
    uint32_t display_count = kernel.log_count < 30 ? kernel.log_count : 30;
    uint32_t start = 0;
    if (kernel.log_count >= 30) {
        start = (kernel.log_head + MAX_LOG_ENTRIES - 30) % MAX_LOG_ENTRIES;
    }
    
    for (uint32_t i = 0; i < display_count; i++) {
        uint32_t idx = (start + i) % MAX_LOG_ENTRIES;
        LogEntry* entry = &kernel.log[idx];
        
        char buf[80];
        snprintf(buf, sizeof(buf), "[%6lu.%03lu] [%s] %s",
                 (uint32_t)(entry->timestamp / 1000),
                 (uint32_t)(entry->timestamp % 1000),
                 level_str[entry->level],
                 entry->message);
        Serial.println(buf);
    }
}

void cmd_top() {
    Serial.println("\n=== System Resources ===");
    Serial.print("CPU:    "); Serial.print(kernel.cpu_usage, 1); Serial.println("%");
    Serial.print("Memory: "); Serial.print(get_used_memory() / 1024); Serial.print("/");
    Serial.print(HEAP_SIZE / 1024); Serial.println(" KB");
    Serial.print("Temp:   "); Serial.print(kernel.temperature, 1); Serial.println("C");
    Serial.print("Uptime: "); Serial.print(kernel.uptime_ms / 1000); Serial.println("s");
}

void cmd_uptime() {
    uint64_t uptime = kernel.uptime_ms;
    uint32_t days = uptime / (1000ULL * 60 * 60 * 24);
    uint32_t hours = (uptime / (1000ULL * 60 * 60)) % 24;
    uint32_t mins = (uptime / (1000ULL * 60)) % 60;
    uint32_t secs = (uptime / 1000ULL) % 60;
    
    Serial.print("Uptime: ");
    Serial.print(days);
    Serial.print("d ");
    Serial.print(hours);
    Serial.print("h ");
    Serial.print(mins);
    Serial.print("m ");
    Serial.print(secs);
    Serial.println("s");
}

void cmd_temp() {
    Serial.print("CPU Temperature: ");
    Serial.print(kernel.temperature, 1);
    Serial.println("C");
}

void cmd_suspend(uint32_t id) {
    if (id >= kernel.task_count) {
        Serial.println("ERROR: Invalid task ID");
        return;
    }
    
    TCB* task = &kernel.tasks[id];
    if (task->state == TASK_TERMINATED) {
        Serial.println("ERROR: Task is terminated");
        return;
    }
    
    if (task->task_type != TASK_TYPE_APPLICATION) {
        Serial.println("ERROR: Can only suspend applications");
        return;
    }
    
    task->state = TASK_SUSPENDED;
    Serial.print("Task '");
    Serial.print(task->name);
    Serial.println("' suspended");
}

void cmd_resume(uint32_t id) {
    if (id >= kernel.task_count) {
        Serial.println("ERROR: Invalid task ID");
        return;
    }
    
    TCB* task = &kernel.tasks[id];
    if (task->state != TASK_SUSPENDED) {
        Serial.println("ERROR: Task is not suspended");
        return;
    }
    
    task->state = TASK_READY;
    Serial.print("Task '");
    Serial.print(task->name);
    Serial.println("' resumed");
}

void cmd_kill(uint32_t id, bool force) {
    if (id >= kernel.task_count) {
        Serial.println("ERROR: Invalid task ID");
        return;
    }
    
    TCB* task = &kernel.tasks[id];
    
    if (task->state == TASK_TERMINATED) {
        Serial.println("ERROR: Task already terminated");
        return;
    }
    
    if (task->task_type != TASK_TYPE_APPLICATION && !force) {
        Serial.println("ERROR: Cannot kill system task");
        Serial.println("Use 'root kill <id>' to force");
        return;
    }
    
    if ((task->flags & TASK_FLAG_PROTECTED) && !force) {
        Serial.println("ERROR: Task is protected");
        Serial.println("Use 'root kill <id>' to force");
        return;
    }
    
    if (force && task->task_type != TASK_TYPE_APPLICATION) {
        Serial.println("*** WARNING: Killing system component! ***");
    }
    
    brutal_task_kill(id);
    
    if (force) {
        kernel.root_mode = false;
        Serial.println("[Root mode auto-disabled]");
    }
}

void cmd_root() {
    kernel.root_mode = !kernel.root_mode;
    if (kernel.root_mode) {
        Serial.println("\n*** ROOT MODE ENABLED ***");
        Serial.println("WARNING: Can now kill protected tasks!");
    } else {
        Serial.println("Root mode disabled");
    }
}

void cmd_reboot() {
    Serial.println("\n*** SYSTEM REBOOT ***");
    Serial.println("Rebooting...");
    delay(1000);
    watchdog_enable(1, 1);
    while(1);
}

void cmd_shutdown() {
    Serial.println("\n*** INITIATING SAFE SHUTDOWN ***");
    
    if (kernel.vfs_mounted && kernel.vfs_sb->file_count > 0) {
        Serial.print("VFS has ");
        Serial.print(kernel.vfs_sb->file_count);
        Serial.println(" unsaved files.");
        Serial.print("Commit to SD? (y/n): ");
        
        unsigned long timeout = millis() + 10000;
        while (millis() < timeout) {
            if (Serial.available()) {
                char c = Serial.read();
                Serial.println(c);
                if (c == 'y' || c == 'Y') {
                    Serial.println("Committing VFS to SD...");
                    
                    if (kernel.fs_mounted) {
                        for (int i = 0; i < VFS_MAX_FILES; i++) {
                            VFSFile* vf = &kernel.vfs_sb->files[i];
                            if (vf->in_use) {
                                char path[64];
                                snprintf(path, sizeof(path), "/vfs_%s", vf->name);
                                
                                int fd = fs_open(path, true);
                                if (fd >= 0) {
                                    char buffer[VFS_MAX_FILE_SIZE];
                                    int bytes = vfs_read(i, buffer, sizeof(buffer));
                                    if (bytes > 0) {
                                        kernel.fs_open_files[fd].handle.write((uint8_t*)buffer, bytes);
                                    }
                                    fs_close(fd);
                                    Serial.print("  Saved: ");
                                    Serial.println(path);
                                }
                            }
                        }
                        Serial.println("VFS committed to SD");
                    } else {
                        Serial.println("ERROR: FS not mounted");
                    }
                    break;
                } else if (c == 'n' || c == 'N') {
                    Serial.println("VFS files discarded");
                    break;
                }
            }
        }
    }
    
    Serial.println("Checking FS integrity...");
    if (kernel.fs_mounted) {
        for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
            if (kernel.fs_open_files[i].open) {
                Serial.print("  Closing: ");
                Serial.println(kernel.fs_open_files[i].path);
                fs_close(i);
            }
        }
        Serial.println("All files closed");
    }
    
    Serial.println("Flushing logs...");
    Serial.flush();
    delay(500);
    
    Serial.println("Stopping services...");
    if (kernel.vfs_mounted) vfs_unmount();
    if (kernel.fs_mounted) fs_unmount();
    
    Serial.println("*** SHUTDOWN COMPLETE ***");
    Serial.println("Safe to power off");
    Serial.flush();
    
    kernel.running = false;
    while(1) {
        __asm__ volatile ("wfi");
    }
}

void cmd_vfscreate() {
    if (kernel.vfs_active) {
        Serial.println("VFS already active");
        return;
    }
    
    kernel.vfs_sb = (VFSSuperblock*)kmalloc(sizeof(VFSSuperblock), 0);
    if (!kernel.vfs_sb) {
        Serial.println("[VFS] Failed to allocate superblock");
        return;
    }
    
    kernel.vfs_data = (uint8_t*)kmalloc(VFS_STORAGE_SIZE, 0);
    if (!kernel.vfs_data) {
        kfree(kernel.vfs_sb);
        kernel.vfs_sb = NULL;
        Serial.println("[VFS] Failed to allocate data buffer");
        return;
    }
    
    kernel.vfs_active = true;
    vfs_mount();
}

void cmd_vfsdedicate() {
    if (!kernel.vfs_mounted) {
        Serial.println("ERROR: VFS not mounted");
        return;
    }
    
    if (!kernel.fs_mounted) {
        Serial.println("ERROR: FS not mounted");
        return;
    }
    
    if (kernel.vfs_sb->file_count == 0) {
        Serial.println("VFS is empty");
        return;
    }
    
    Serial.println("Saving VFS files to SD...");
    
    uint32_t saved = 0;
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        VFSFile* vf = &kernel.vfs_sb->files[i];
        if (vf->in_use) {
            char path[64];
            snprintf(path, sizeof(path), "/vfs_%s", vf->name);
            
            int fd = fs_open(path, true);
            if (fd >= 0) {
                char buffer[VFS_MAX_FILE_SIZE];
                int bytes = vfs_read(i, buffer, sizeof(buffer));
                if (bytes > 0) {
                    kernel.fs_open_files[fd].handle.write((uint8_t*)buffer, bytes);
                    saved++;
                }
                fs_close(fd);
                Serial.print("  Saved: ");
                Serial.println(path);
            }
        }
    }
    
    Serial.print("Dedicated ");
    Serial.print(saved);
    Serial.println(" files to SD");
}

void cmd_vfsmkfile(char* name, uint8_t type) {
    if (!kernel.vfs_mounted) {
        Serial.println("ERROR: VFS not mounted. Use 'vfscreate' first");
        return;
    }
    
    int fd = vfs_create(name, type, kernel.current_task);
    if (fd >= 0) {
        Serial.print("Created VFS file: ");
        Serial.print(name);
        Serial.print(" (fd=");
        Serial.print(fd);
        Serial.println(")");
        
        char sample[64];
        snprintf(sample, sizeof(sample), "Sample data for %s\n", name);
        int written = vfs_write(fd, sample, strlen(sample));
        if (written > 0) {
            Serial.print("Wrote ");
            Serial.print(written);
            Serial.println(" bytes");
        }
    }
}

void cmd_vfsrm(int fd) {
    if (!kernel.vfs_mounted) {
        Serial.println("ERROR: VFS not mounted");
        return;
    }
    vfs_delete(fd);
}

void cmd_vfscat(int fd) {
    if (!kernel.vfs_mounted) {
        Serial.println("ERROR: VFS not mounted");
        return;
    }
    
    if (fd < 0 || fd >= VFS_MAX_FILES) {
        Serial.println("ERROR: Invalid file descriptor");
        return;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    if (!file->in_use) {
        Serial.println("ERROR: File not in use");
        return;
    }
    
    char buffer[512];
    int bytes = vfs_read(fd, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        Serial.println("\n=== VFS File Contents ===");
        Serial.println(buffer);
        Serial.println("=== End ===");
    } else {
        Serial.println("ERROR: Read failed or empty file");
    }
}

void cmd_write(char* path, char* text) {
    if (!kernel.fs_mounted) {
        Serial.println("ERROR: FS not mounted");
        return;
    }
    
    int fd = fs_open(path, true);
    if (fd < 0) {
        Serial.println("ERROR: Failed to open file");
        return;
    }
    
    int written = fs_write_str(fd, text);
    fs_write_str(fd, "\n");
    fs_close(fd);
    
    Serial.print("Wrote ");
    Serial.print(written);
    Serial.print(" bytes to ");
    Serial.println(path);
}

void shell_execute(char* cmd) {
    if (strlen(cmd) == 0) return;
    
    if (strncmp(cmd, "root kill ", 10) == 0) {
        if (!kernel.root_mode) {
            Serial.println("ERROR: Root mode not enabled");
        } else {
            uint32_t id = atoi(cmd + 10);
            cmd_kill(id, true);
        }
        return;
    }
    
    if (strcmp(cmd, "help") == 0) cmd_help();
    else if (strcmp(cmd, "arch") == 0) cmd_arch();
    else if (strcmp(cmd, "oom") == 0) cmd_oom();
    else if (strcmp(cmd, "ps") == 0) cmd_ps();
    else if (strcmp(cmd, "listapps") == 0) cmd_listapps();
    else if (strcmp(cmd, "listmods") == 0) cmd_listmods();
    else if (strcmp(cmd, "mem") == 0) cmd_mem();
    else if (strcmp(cmd, "memmap") == 0) cmd_memmap();
    else if (strcmp(cmd, "compact") == 0) cmd_compact();
    else if (strcmp(cmd, "dmesg") == 0) cmd_dmesg();
    else if (strcmp(cmd, "top") == 0) cmd_top();
    else if (strcmp(cmd, "uptime") == 0) cmd_uptime();
    else if (strcmp(cmd, "temp") == 0) cmd_temp();
    else if (strcmp(cmd, "root") == 0) cmd_root();
    else if (strcmp(cmd, "reboot") == 0) cmd_reboot();
    else if (strcmp(cmd, "shutdown") == 0) cmd_shutdown();
    else if (strcmp(cmd, "vfscreate") == 0) cmd_vfscreate();
    else if (strcmp(cmd, "vfsls") == 0) vfs_list();
    else if (strcmp(cmd, "vfsstat") == 0) vfs_stats();
    else if (strcmp(cmd, "vfsdedicate") == 0) cmd_vfsdedicate();
    else if (strcmp(cmd, "ls") == 0) fs_list("/");
    else if (strcmp(cmd, "stat") == 0) fs_stats();
    else if (strcmp(cmd, "clear") == 0) {
        Serial.write(0x1B); Serial.print("[2J");
        Serial.write(0x1B); Serial.print("[H");
        if (kernel.display_alive) {
            tft.fillScreen(ILI9341_BLACK);
            last_tasks_str[0] = '\0';
        }
    }
    else if (strcmp(cmd, "snake") == 0) {
        extern void snake_spawn();
        snake_spawn();
    }
    else if (strcmp(cmd, "calc") == 0) {
        extern void calc_spawn();
        calc_spawn();
    }
    else if (strcmp(cmd, "clock") == 0) {
        extern void clock_spawn();
        clock_spawn();
    }
    else if (strcmp(cmd, "sysmon") == 0) {
        extern void sysmon_spawn();
        sysmon_spawn();
    }
    else if (strcmp(cmd, "memhog") == 0) {
        extern void memhog_spawn();
        memhog_spawn();
    }
    else if (strcmp(cmd, "cpuburn") == 0) {
        extern void cpuburn_spawn();
        cpuburn_spawn();
    }
    else if (strcmp(cmd, "stress") == 0) {
        extern void stress_spawn();
        stress_spawn();
    }
    else if (strncmp(cmd, "kill ", 5) == 0) {
        uint32_t id = atoi(cmd + 5);
        cmd_kill(id, false);
    }
    else if (strncmp(cmd, "suspend ", 8) == 0) {
        uint32_t id = atoi(cmd + 8);
        cmd_suspend(id);
    }
    else if (strncmp(cmd, "resume ", 7) == 0) {
        uint32_t id = atoi(cmd + 7);
        cmd_resume(id);
    }
    else if (strncmp(cmd, "vfsmkfile ", 10) == 0) {
        char* name = strtok(cmd + 10, " ");
        char* type_str = strtok(NULL, " ");
        if (name && type_str) {
            uint8_t type = atoi(type_str);
            cmd_vfsmkfile(name, type);
        } else {
            Serial.println("Usage: vfsmkfile <name> <type>");
        }
    }
    else if (strncmp(cmd, "vfsrm ", 6) == 0) {
        int fd = atoi(cmd + 6);
        cmd_vfsrm(fd);
    }
    else if (strncmp(cmd, "vfscat ", 7) == 0) {
        int fd = atoi(cmd + 7);
        cmd_vfscat(fd);
    }
    else if (strncmp(cmd, "ls ", 3) == 0) {
        fs_list(cmd + 3);
    }
    else if (strncmp(cmd, "mkdir ", 6) == 0) {
        if (fs_mkdir(cmd + 6)) {
            Serial.println("Directory created");
        } else {
            Serial.println("Failed to create directory");
        }
    }
    else if (strncmp(cmd, "rm ", 3) == 0) {
        if (fs_remove(cmd + 3)) {
            Serial.println("File deleted");
        } else {
            Serial.println("Failed to delete file");
        }
    }
    else if (strncmp(cmd, "cat ", 4) == 0) {
        fs_cat(cmd + 4);
    }
    else if (strncmp(cmd, "write ", 6) == 0) {
        char* path = strtok(cmd + 6, " ");
        char* text = strtok(NULL, "");
        if (path && text) {
            cmd_write(path, text);
        } else {
            Serial.println("Usage: write <path> <text>");
        }
    }
    else if (strcmp(cmd, "logcat") == 0) {
        fs_cat(FS_LOG_FILE);
    }
    else {
        Serial.print("Unknown: ");
        Serial.println(cmd);
        Serial.println("Type 'help'");
    }
}

void shell_prompt() {
    if (kernel.root_mode) {
        Serial.print("\033[1;31mrp2040#\033[0m ");
    } else {
        Serial.print("rp2040> ");
    }
}

void idle_task(void* arg);
void display_task(void* arg);
void display_init();
void display_deinit();
void shell_task(void* arg);
void shell_deinit();
void input_task(void* arg);
void input_deinit();
void cpu_monitor_task(void* arg);
void cpumon_deinit();
void temp_monitor_task(void* arg);
void tempmon_deinit();
void vfs_task(void* arg);
void vfs_deinit();
void fs_task(void* arg);
void fs_deinit();

ModuleCallbacks display_callbacks = {
    .init = display_init,
    .tick = display_task,
    .deinit = display_deinit
};

ModuleCallbacks shell_callbacks = {
    .init = NULL,
    .tick = shell_task,
    .deinit = shell_deinit
};

ModuleCallbacks input_callbacks = {
    .init = NULL,
    .tick = input_task,
    .deinit = input_deinit
};

ModuleCallbacks cpumon_callbacks = {
    .init = NULL,
    .tick = cpu_monitor_task,
    .deinit = cpumon_deinit
};

ModuleCallbacks tempmon_callbacks = {
    .init = NULL,
    .tick = temp_monitor_task,
    .deinit = tempmon_deinit
};

ModuleCallbacks vfs_callbacks = {
    .init = NULL,
    .tick = vfs_task,
    .deinit = vfs_deinit
};

ModuleCallbacks fs_callbacks = {
    .init = NULL,
    .tick = fs_task,
    .deinit = fs_deinit
};

void idle_task(void* arg) {
    task_sleep(100);
}

void display_init() {
    pinMode(LCD_LED, OUTPUT);
    digitalWrite(LCD_LED, HIGH);
    
    SPI.setRX(16);
    SPI.setTX(19);
    SPI.setSCK(18);
    
    tft.begin();
    tft.setRotation(3);
    tft.fillScreen(ILI9341_BLACK);
    
    tft.fillRect(0, 0, LCD_WIDTH, 50, ILI9341_BLUE);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 5);
    tft.print("RP2040 Kernel v8");
    
    Serial.println("[DISPLAY] Initialized");
}

void display_task(void* arg) {
    if (!kernel.display_alive) {
        task_sleep(10000);
        return;
    }
    
    static uint32_t last_update = 0;
    uint32_t now = get_time_ms();
    
    if (now - last_update < 500) {
        task_sleep(100);
        return;
    }
    last_update = now;
    
    char buf[64];
    
    uint32_t active_tasks = 0;
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        if (kernel.tasks[i].state != TASK_TERMINATED) {
            active_tasks++;
        }
    }
    
    snprintf(buf, sizeof(buf), "Tasks: %d", active_tasks);
    if (strcmp(buf, last_tasks_str) != 0) {
        tft.fillRect(10, 25, 150, 12, ILI9341_BLUE);
        tft.setTextColor(ILI9341_YELLOW);
        tft.setTextSize(1);
        tft.setCursor(10, 25);
        tft.print(buf);
        strcpy(last_tasks_str, buf);
    }
    
    snprintf(buf, sizeof(buf), "Up: %lus", kernel.uptime_ms / 1000);
    if (strcmp(buf, last_uptime_str) != 0) {
        tft.fillRect(170, 25, 140, 12, ILI9341_BLUE);
        tft.setTextColor(ILI9341_YELLOW);
        tft.setCursor(170, 25);
        tft.print(buf);
        strcpy(last_uptime_str, buf);
    }
    
    uint32_t mem_pct = (get_used_memory() * 100) / HEAP_SIZE;
    snprintf(buf, sizeof(buf), "Mem: %d%%", mem_pct);
    if (strcmp(buf, last_mem_str) != 0) {
        tft.fillRect(10, 38, 140, 12, ILI9341_BLUE);
        tft.setCursor(10, 38);
        uint16_t color = mem_pct > 80 ? ILI9341_RED : ILI9341_YELLOW;
        tft.setTextColor(color);
        tft.print(buf);
        strcpy(last_mem_str, buf);
    }
    
    snprintf(buf, sizeof(buf), "CPU: %.0f%%", kernel.cpu_usage);
    if (strcmp(buf, last_cpu_str) != 0) {
        tft.fillRect(170, 38, 70, 12, ILI9341_BLUE);
        tft.setTextColor(ILI9341_YELLOW);
        tft.setCursor(170, 38);
        tft.print(buf);
        strcpy(last_cpu_str, buf);
    }
    
    snprintf(buf, sizeof(buf), "%.1fC", kernel.temperature);
    if (strcmp(buf, last_temp_str) != 0) {
        tft.fillRect(245, 38, 65, 12, ILI9341_BLUE);
        uint16_t temp_color = kernel.temperature > 50.0f ? ILI9341_RED : ILI9341_CYAN;
        tft.setTextColor(temp_color);
        tft.setCursor(245, 38);
        tft.print(buf);
        strcpy(last_temp_str, buf);
    }
    
    task_sleep(500);
}

void display_deinit() {
    Serial.println("[DISPLAY] DEINIT");
    digitalWrite(LCD_LED, LOW);
    kernel.display_alive = false;
}

void shell_task(void* arg) {
    if (!kernel.shell_alive) {
        task_sleep(10000);
        return;
    }
    
    while (Serial.available()) {
        int c = Serial.read();
        
        if (c == '\r' || c == '\n') {
            Serial.println();
            cmd_buffer[cmd_pos] = '\0';
            shell_execute(cmd_buffer);
            cmd_pos = 0;
            
            if (!kernel.shell_alive) {
                task_sleep(10000);
                return;
            }
            
            shell_prompt();
        } else if (c == '\b' || c == 127) {
            if (cmd_pos > 0) {
                cmd_pos--;
                Serial.write('\b');
                Serial.write(' ');
                Serial.write('\b');
            }
        } else if (cmd_pos < sizeof(cmd_buffer) - 1) {
            cmd_buffer[cmd_pos++] = c;
            Serial.write(c);
        }
    }
    
    task_sleep(10);
}

void shell_deinit() {
    Serial.println("[SHELL] DEINIT");
    kernel.shell_alive = false;
    cmd_pos = 0;
}

void input_task(void* arg) {
    if (!kernel.input_alive) {
        task_sleep(10000);
        return;
    }
    
    static uint8_t last_state = 0xFF;
    uint8_t state = 0;
    
    if (gpio_read_fast(BTN_LEFT)) state |= 0x01;
    if (gpio_read_fast(BTN_RIGHT)) state |= 0x02;
    if (gpio_read_fast(BTN_TOP)) state |= 0x04;
    if (gpio_read_fast(BTN_BOTTOM)) state |= 0x08;
    if (gpio_read_fast(BTN_SELECT)) state |= 0x10;
    if (gpio_read_fast(BTN_START)) state |= 0x20;
    if (gpio_read_fast(BTN_A)) state |= 0x40;
    if (gpio_read_fast(BTN_B)) state |= 0x80;
    
    uint8_t pressed = (state ^ last_state) & state;
    if (pressed && kernel.shell_alive) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Button: 0x%02X", pressed);
        Serial.println(buf);
    }
    
    last_state = state;
    task_sleep(50);
}

void input_deinit() {
    Serial.println("[INPUT] DEINIT");
    kernel.input_alive = false;
}

void cpu_monitor_task(void* arg) {
    if (!kernel.cpumon_alive) {
        task_sleep(10000);
        return;
    }
    
    static uint32_t last_idle = 0;
    static uint32_t last_total = 0;
    
    uint32_t total = 0;
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        if (kernel.tasks[i].state != TASK_TERMINATED) {
            total += kernel.tasks[i].cpu_time;
        }
    }
    
    uint32_t idle = kernel.tasks[0].cpu_time;
    uint32_t total_delta = total - last_total;
    uint32_t idle_delta = idle - last_idle;
    
    if (total_delta > 0) {
        kernel.cpu_usage = 100.0f - ((idle_delta * 100.0f) / total_delta);
        if (kernel.cpu_usage < 0) kernel.cpu_usage = 0;
        if (kernel.cpu_usage > 100) kernel.cpu_usage = 100;
    }
    
    last_idle = idle;
    last_total = total;
    
    task_sleep(1000);
}

void cpumon_deinit() {
    Serial.println("[CPUMON] DEINIT");
    kernel.cpumon_alive = false;
}

void temp_monitor_task(void* arg) {
    if (!kernel.tempmon_alive) {
        task_sleep(10000);
        return;
    }
    
    kernel.temperature = read_temperature();
    task_sleep(2000);
}

void tempmon_deinit() {
    Serial.println("[TEMPMON] DEINIT");
    kernel.tempmon_alive = false;
}

void vfs_task(void* arg) {
    if (!kernel.vfs_alive) {
        task_sleep(10000);
        return;
    }
    
    static uint32_t last_maintenance = 0;
    uint32_t now = get_time_ms();
    
    if (now - last_maintenance > 30000) {
        if (kernel.vfs_mounted) {
            last_maintenance = now;
        }
    }
    
    task_sleep(5000);
}

void vfs_deinit() {
    Serial.println("[VFS] DEINIT");
    vfs_unmount();
    
    if (kernel.vfs_data) {
        kfree(kernel.vfs_data);
        kernel.vfs_data = NULL;
    }
    
    if (kernel.vfs_sb) {
        kfree(kernel.vfs_sb);
        kernel.vfs_sb = NULL;
    }
    
    kernel.vfs_alive = false;
}

void fs_task(void* arg) {
    if (!kernel.fs_alive) {
        task_sleep(10000);
        return;
    }
    
    static uint32_t last_check = 0;
    uint32_t now = get_time_ms();
    
    if (now - last_check > 60000) {
        if (kernel.fs_mounted) {
            uint64_t used = 0;
            File root = SD.open("/");
            if (root) {
                File file = root.openNextFile();
                while (file) {
                    if (!file.isDirectory()) {
                        used += file.size();
                    }
                    file.close();
                    file = root.openNextFile();
                }
                root.close();
                kernel.fs_used_bytes = used;
            }
            last_check = now;
        }
    }
    
    task_sleep(10000);
}

void fs_deinit() {
    Serial.println("[FS] DEINIT");
    fs_unmount();
    kernel.fs_alive = false;
}

void counter_task(void* arg) {
    static uint32_t counter = 0;
    counter++;
    task_sleep(1000);
}

void watchdog_task(void* arg) {
    static float max_mem = 0;
    static float max_temp = 0;
    
    float mem_usage = (get_used_memory() * 100.0f) / HEAP_SIZE;
    if (mem_usage > max_mem) {
        max_mem = mem_usage;
        if (mem_usage > 90.0f) {
            klog(2, "WATCHDOG: High memory!");
        }
    }
    
    if (kernel.temperature > max_temp) {
        max_temp = kernel.temperature;
        if (kernel.temperature > 60.0f) {
            klog(2, "WATCHDOG: High temp!");
        }
    }
    
    if (kernel.fragmentation_pct > 70) {
        klog(1, "WATCHDOG: High fragmentation");
    }
    
    task_sleep(5000);
}

void addModules() {
    Serial.println("\n=== Loading User Modules ===");
    
    task_create("counter", counter_task, NULL, 5,
                TASK_TYPE_MODULE, 0, 0,
                OOM_PRIORITY_NEVER, 1 * 1024, NULL,
                "Simple counter module");
    Serial.println("[OK] Counter module");
    
    task_create("watchdog", watchdog_task, NULL, 3,
                TASK_TYPE_MODULE, TASK_FLAG_PROTECTED, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, NULL,
                "System health watchdog");
    Serial.println("[OK] Watchdog module");
    
    Serial.println("=== Module Loading Complete ===\n");
}

void memhog_spawn();
void cpuburn_spawn();
void stress_spawn();
void snake_spawn();
void calc_spawn();
void clock_spawn();
void sysmon_spawn();

void memhog_task(void* arg) {
    TCB* me = &kernel.tasks[kernel.current_task];
    
    if (me->state == TASK_TERMINATED) {
        task_sleep(10000);
        return;
    }
    
    Serial.println("\n[MEMHOG] Starting memory stress test");
    klog(1, "MEMHOG: Started");
    
    void* blocks[50];
    uint32_t count = 0;
    
    while (count < 50) {
        if (me->state == TASK_TERMINATED) {
            Serial.println("[MEMHOG] Killed");
            task_sleep(10000);
            return;
        }
        
        size_t size = 2048 + (random(0, 8192));
        void* ptr = kmalloc(size, kernel.current_task);
        
        if (ptr) {
            blocks[count++] = ptr;
            memset(ptr, 0xAA, size);
            if (count % 5 == 0) {
                Serial.print("[MEMHOG] Block ");
                Serial.print(count);
                Serial.print(": ");
                Serial.print(me->mem_used / 1024);
                Serial.println(" KB");
            }
            task_sleep(200);
        } else {
            Serial.println("[MEMHOG] Allocation failed!");
            break;
        }
    }
    
    if (me->state != TASK_TERMINATED) {
        Serial.println("[MEMHOG] Cleaning up");
        for (uint32_t i = 0; i < count; i++) {
            kfree(blocks[i]);
        }
        Serial.print("[MEMHOG] Peak: ");
        Serial.print(me->mem_peak / 1024);
        Serial.println(" KB");
        klog(0, "MEMHOG: Completed");
        me->state = TASK_TERMINATED;
        me->last_run = get_time_ms();
    }
    
    task_sleep(10000);
}

void memhog_spawn() {
    uint32_t tid = task_create("memhog", memhog_task, NULL, 5,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 30000,
                               OOM_PRIORITY_LOW, 50 * 1024, NULL,
                               "Memory stress test (OOM=LOW)");
    if (tid > 0) {
        Serial.println("Memory stress test spawned");
    }
}

void cpuburn_task(void* arg) {
    TCB* me = &kernel.tasks[kernel.current_task];
    
    if (me->state == TASK_TERMINATED) {
        task_sleep(10000);
        return;
    }
    
    Serial.println("\n[CPUBURN] Starting CPU stress");
    klog(1, "CPUBURN: Started");
    
    for (int iter = 0; iter < 100; iter++) {
        if (me->state == TASK_TERMINATED) {
            Serial.println("[CPUBURN] Killed");
            task_sleep(10000);
            return;
        }
        
        uint32_t primes = 0;
        for (uint32_t n = 2; n < 2000; n++) {
            bool is_prime = true;
            for (uint32_t i = 2; i * i <= n; i++) {
                if (n % i == 0) {
                    is_prime = false;
                    break;
                }
            }
            if (is_prime) primes++;
        }
        
        if (iter % 20 == 0) {
            Serial.print("[CPUBURN] ");
            Serial.print(iter);
            Serial.println("%");
        }
        
        task_sleep(5);
    }
    
    if (me->state != TASK_TERMINATED) {
        Serial.println("[CPUBURN] Complete");
        klog(0, "CPUBURN: Completed");
        me->state = TASK_TERMINATED;
        me->last_run = get_time_ms();
    }
    
    task_sleep(10000);
}

void cpuburn_spawn() {
    uint32_t tid = task_create("cpuburn", cpuburn_task, NULL, 5,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 30000,
                               OOM_PRIORITY_NORMAL, 4 * 1024, NULL,
                               "CPU stress test (OOM=NORM)");
    if (tid > 0) {
        Serial.println("CPU stress test spawned");
    }
}

void stress_task(void* arg) {
    TCB* me = &kernel.tasks[kernel.current_task];
    
    Serial.println("\n[STRESS] Full system stress test");
    klog(2, "STRESS: Started");
    
    void* blocks[20];
    uint32_t block_count = 0;
    
    for (uint32_t cycle = 0; cycle < 20; cycle++) {
        if (me->state == TASK_TERMINATED) {
            Serial.println("[STRESS] Terminated");
            task_sleep(10000);
            return;
        }
        
        size_t size = 1024 + random(0, 4096);
        void* ptr = kmalloc(size, kernel.current_task);
        if (ptr && block_count < 20) {
            blocks[block_count++] = ptr;
            memset(ptr, 0xBB, size);
        }
        
        uint32_t sum = 0;
        for (uint32_t i = 0; i < 10000; i++) {
            sum += i * i;
        }
        
        Serial.print("[STRESS] Cycle ");
        Serial.print(cycle);
        Serial.println("/20");
        
        task_sleep(100);
        
        if (block_count > 10 && random(0, 100) < 30) {
            uint32_t idx = random(0, block_count);
            kfree(blocks[idx]);
            blocks[idx] = blocks[--block_count];
        }
    }
    
    Serial.println("[STRESS] Cleanup");
    for (uint32_t i = 0; i < block_count; i++) {
        kfree(blocks[i]);
    }
    
    Serial.println("[STRESS] Complete!");
    klog(0, "STRESS: Completed");
    
    me->state = TASK_TERMINATED;
    me->last_run = get_time_ms();
    task_sleep(10000);
}

void stress_spawn() {
    uint32_t tid = task_create("stress", stress_task, NULL, 4,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 60000,
                               OOM_PRIORITY_LOW, 100 * 1024, NULL,
                               "System stress test (OOM=LOW)");
    if (tid > 0) {
        Serial.println("System stress test spawned");
    }
}

struct SnakeGame {
    int16_t snake_x[50];
    int16_t snake_y[50];
    uint8_t snake_len;
    int8_t dir_x, dir_y;
    int16_t food_x, food_y;
    uint32_t score;
    bool game_over;
};

SnakeGame* snake_game = NULL;

void snake_init() {
    snake_game = (SnakeGame*)kmalloc(sizeof(SnakeGame), kernel.current_task);
    if (!snake_game) {
        Serial.println("[SNAKE] Alloc failed!");
        return;
    }
    
    memset(snake_game, 0, sizeof(SnakeGame));
    snake_game->snake_len = 3;
    snake_game->snake_x[0] = 160;
    snake_game->snake_y[0] = 120;
    snake_game->dir_x = 10;
    snake_game->dir_y = 0;
    snake_game->food_x = random(60, 260);
    snake_game->food_y = random(60, 180);
    
    if (kernel.display_alive) {
        tft.fillRect(0, 50, LCD_WIDTH, LCD_HEIGHT - 50, ILI9341_BLACK);
        tft.setTextColor(ILI9341_GREEN);
        tft.setTextSize(1);
        tft.setCursor(10, 55);
        tft.print("SNAKE - Use buttons");
    }
    
    Serial.println("[SNAKE] Game started");
    klog(0, "SNAKE: Started");
}

void snake_task(void* arg) {
    if (!snake_game || snake_game->game_over) {
        task_sleep(5000);
        return;
    }
    
    snake_game->snake_x[0] += snake_game->dir_x;
    snake_game->snake_y[0] += snake_game->dir_y;
    
    if (abs(snake_game->snake_x[0] - snake_game->food_x) < 10 &&
        abs(snake_game->snake_y[0] - snake_game->food_y) < 10) {
        snake_game->score++;
        snake_game->food_x = random(60, 260);
        snake_game->food_y = random(60, 180);
    }
    
    if (snake_game->snake_x[0] < 60 || snake_game->snake_x[0] > 260 ||
        snake_game->snake_y[0] < 60 || snake_game->snake_y[0] > 180) {
        snake_game->game_over = true;
        Serial.print("[SNAKE] Game Over! Score: ");
        Serial.println(snake_game->score);
    }
    
    task_sleep(100);
}

void snake_deinit() {
    Serial.println("[SNAKE] Deinit");
    if (snake_game) {
        kfree(snake_game);
        snake_game = NULL;
    }
    klog(0, "SNAKE: Ended");
}

ModuleCallbacks snake_callbacks = {
    .init = snake_init,
    .tick = snake_task,
    .deinit = snake_deinit
};

void snake_spawn() {
    uint32_t tid = task_create("snake", snake_task, NULL, 6,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 60000,
                               OOM_PRIORITY_NORMAL, 4 * 1024, &snake_callbacks,
                               "Snake game (OOM=NORM)");
    if (tid > 0) {
        Serial.println("Snake game started");
    }
}

void calc_task(void* arg) {
    task_sleep(100);
}

void calc_init() {
    Serial.println("\n=== CALCULATOR ===");
    Serial.println("Simple calculator app");
    klog(0, "CALC: Started");
}

void calc_deinit() {
    Serial.println("[CALC] Exiting");
    klog(0, "CALC: Exited");
}

ModuleCallbacks calc_callbacks = {
    .init = calc_init,
    .tick = calc_task,
    .deinit = calc_deinit
};

void calc_spawn() {
    uint32_t tid = task_create("calc", calc_task, NULL, 7,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_NORMAL, 4 * 1024, &calc_callbacks,
                               "Calculator (OOM=NORM)");
    if (tid > 0) {
        Serial.println("Calculator started");
    }
}

struct ClockData {
    uint32_t start_time;
};

ClockData* clock_data = NULL;

void clock_init() {
    clock_data = (ClockData*)kmalloc(sizeof(ClockData), kernel.current_task);
    if (!clock_data) return;
    
    clock_data->start_time = get_time_ms();
    
    if (kernel.display_alive) {
        tft.fillRect(0, 50, LCD_WIDTH, LCD_HEIGHT - 50, ILI9341_BLACK);
    }
    
    Serial.println("[CLOCK] Started");
    klog(0, "CLOCK: Started");
}

void clock_task(void* arg) {
    if (!clock_data) {
        task_sleep(1000);
        return;
    }
    
    uint32_t elapsed = (get_time_ms() - clock_data->start_time) / 1000;
    uint8_t hours = (elapsed / 3600) % 24;
    uint8_t mins = (elapsed / 60) % 60;
    uint8_t secs = elapsed % 60;
    
    if (kernel.display_alive) {
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hours, mins, secs);
        
        tft.fillRect(80, 100, 160, 40, ILI9341_BLACK);
        tft.setTextColor(ILI9341_CYAN);
        tft.setTextSize(3);
        tft.setCursor(80, 100);
        tft.print(time_str);
    }
    
    task_sleep(1000);
}

void clock_deinit() {
    Serial.println("[CLOCK] Stopped");
    if (clock_data) {
        kfree(clock_data);
        clock_data = NULL;
    }
    klog(0, "CLOCK: Stopped");
}

ModuleCallbacks clock_callbacks = {
    .init = clock_init,
    .tick = clock_task,
    .deinit = clock_deinit
};

void clock_spawn() {
    uint32_t tid = task_create("clock", clock_task, NULL, 6,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_NORMAL, 2 * 1024, &clock_callbacks,
                               "Digital clock (OOM=NORM)");
    if (tid > 0) {
        Serial.println("Digital clock started");
    }
}

void sysmon_init() {
    if (kernel.display_alive) {
        tft.fillRect(0, 50, LCD_WIDTH, LCD_HEIGHT - 50, ILI9341_BLACK);
        tft.setTextColor(ILI9341_GREEN);
        tft.setTextSize(1);
        tft.setCursor(10, 55);
        tft.print("=== SYSTEM MONITOR ===");
    }
    
    Serial.println("[SYSMON] Started");
    klog(0, "SYSMON: Started");
}

void sysmon_task(void* arg) {
    static uint32_t last_update = 0;
    uint32_t now = get_time_ms();
    
    if (now - last_update < 1000) {
        task_sleep(100);
        return;
    }
    last_update = now;
    
    if (!kernel.display_alive) {
        task_sleep(1000);
        return;
    }
    
    tft.fillRect(0, 70, LCD_WIDTH, 160, ILI9341_BLACK);
    tft.setTextColor(ILI9341_YELLOW);
    tft.setTextSize(1);
    
    char buf[64];
    
    tft.setCursor(10, 70);
    snprintf(buf, sizeof(buf), "CPU: %.1f%%", kernel.cpu_usage);
    tft.print(buf);
    
    tft.setCursor(10, 85);
    uint32_t mem_pct = (get_used_memory() * 100) / HEAP_SIZE;
    snprintf(buf, sizeof(buf), "MEM: %d%%", mem_pct);
    tft.print(buf);
    
    tft.setCursor(10, 100);
    snprintf(buf, sizeof(buf), "FRAG: %d%%", kernel.fragmentation_pct);
    tft.print(buf);
    
    uint32_t active = 0;
    for (uint32_t i = 0; i < kernel.task_count; i++) {
        if (kernel.tasks[i].state != TASK_TERMINATED) active++;
    }
    tft.setCursor(10, 115);
    snprintf(buf, sizeof(buf), "TASKS: %d/%d", active, kernel.task_count);
    tft.print(buf);
    
    tft.setCursor(10, 130);
    snprintf(buf, sizeof(buf), "TEMP: %.1fC", kernel.temperature);
    tft.print(buf);
    
    tft.setCursor(10, 145);
    snprintf(buf, sizeof(buf), "UP: %lus", kernel.uptime_ms / 1000);
    tft.print(buf);
    
    tft.setCursor(10, 160);
    snprintf(buf, sizeof(buf), "OOM: %d", kernel.oom_kills);
    tft.print(buf);
    
    tft.setCursor(10, 175);
    if (kernel.vfs_mounted) {
        snprintf(buf, sizeof(buf), "VFS: %d files", kernel.vfs_sb->file_count);
    } else {
        snprintf(buf, sizeof(buf), "VFS: Inactive");
    }
    tft.print(buf);
    
    tft.setCursor(10, 190);
    if (kernel.fs_mounted) {
        snprintf(buf, sizeof(buf), "SD: %luMB", kernel.fs_total_bytes / (1024 * 1024));
    } else {
        snprintf(buf, sizeof(buf), "SD: Unavailable");
    }
    tft.print(buf);
    
    task_sleep(1000);
}

void sysmon_deinit() {
    Serial.println("[SYSMON] Stopped");
    klog(0, "SYSMON: Stopped");
}

ModuleCallbacks sysmon_callbacks = {
    .init = sysmon_init,
    .tick = sysmon_task,
    .deinit = sysmon_deinit
};

void sysmon_spawn() {
    uint32_t tid = task_create("sysmon", sysmon_task, NULL, 5,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_HIGH, 2 * 1024, &sysmon_callbacks,
                               "System monitor (OOM=HIGH)");
    if (tid > 0) {
        Serial.println("System monitor started");
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n\n========================================");
    Serial.println("  RP2040 Kernel v8 - SD FS Edition");
    Serial.println("  Picomimi Kernel v8");
    Serial.println("========================================");
    Serial.println("Initializing...");
    
    uint8_t buttons[] = {BTN_LEFT, BTN_RIGHT, BTN_TOP, BTN_BOTTOM, 
                         BTN_SELECT, BTN_START, BTN_A, BTN_B, BTN_ONOFF};
    for (int i = 0; i < 9; i++) {
        pinMode(buttons[i], INPUT_PULLUP);
    }
    Serial.println("[OK] Input system");
    
    temp_init();
    kernel.temperature = read_temperature();
    Serial.print("[OK] Temperature (");
    Serial.print(kernel.temperature, 1);
    Serial.println("C)");
    
    mem_init();
    Serial.println("[OK] Memory manager");
    
    task_init();
    Serial.println("[OK] Task scheduler");
    
    vfs_init();
    Serial.println("[OK] VFS initialized (inactive)");
    
    fs_init();
    if (kernel.fs_available) {
        fs_mount();
    }
    
    Serial.println("\n=== Loading System Tasks ===");
    
    task_create("idle", idle_task, NULL, 0,
                TASK_TYPE_KERNEL, TASK_FLAG_PROTECTED | TASK_FLAG_CRITICAL | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 0, NULL,
                "Kernel idle task");
    Serial.println("[OK] Idle (KERNEL - CRITICAL!)");
    
    task_create("display", display_task, NULL, 2,
                TASK_TYPE_DRIVER, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 4 * 1024, &display_callbacks,
                "ILI9341 display driver");
    Serial.println("[OK] Display driver");
    
    task_create("input", input_task, NULL, 4,
                TASK_TYPE_DRIVER, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &input_callbacks,
                "Button input driver");
    Serial.println("[OK] Input driver");
    
    task_create("shell", shell_task, NULL, 3,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 4 * 1024, &shell_callbacks,
                "Command shell service");
    Serial.println("[OK] Shell service");
    
    task_create("cpumon", cpu_monitor_task, NULL, 1,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &cpumon_callbacks,
                "CPU usage monitor");
    Serial.println("[OK] CPU monitor");
    
    task_create("tempmon", temp_monitor_task, NULL, 1,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &tempmon_callbacks,
                "Temperature monitor");
    Serial.println("[OK] Temp monitor");
    
    if (kernel.fs_available) {
        task_create("fs", fs_task, NULL, 2,
                    TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                    OOM_PRIORITY_NEVER, 4 * 1024, &fs_callbacks,
                    "SD filesystem service");
        Serial.println("[OK] FS service");
    }
    
    addModules();
    
    Serial.println("\n========================================");
    Serial.println("Kernel boot complete!");
    Serial.println("========================================");
    Serial.print("Heap:      "); Serial.print(HEAP_SIZE / 1024); Serial.println(" KB");
    Serial.print("Tasks:     "); Serial.print(kernel.task_count); Serial.println(" loaded");
    Serial.print("VFS:       Inactive (use 'vfscreate')");
    Serial.println();
    if (kernel.fs_available) {
        Serial.print("SD Card:   "); 
        Serial.print(kernel.fs_total_bytes / (1024 * 1024)); 
        Serial.println(" MB");
    } else {
        Serial.println("SD Card:   Unavailable");
    }
    
    Serial.println("\n=== Task Architecture ===");
    Serial.println("KERNEL:   Core system - IMMORTAL");
    Serial.println("DRIVER:   Hardware - IMMORTAL");
    Serial.println("SERVICE:  System services - IMMORTAL");
    Serial.println("MODULE:   Extensions - OOM PROTECTED");
    Serial.println("APP:      User programs - OOM KILLABLE");
    
    Serial.println("\n=== Storage Systems ===");
    Serial.println("VFS: RAM-based temporary filesystem");
    Serial.println("     Use 'vfscreate' to activate");
    Serial.println("     Use 'vfsdedicate' to save to SD");
    if (kernel.fs_available) {
        Serial.println("FS:  SD card persistent storage");
        Serial.println("     Ready for use");
    } else {
        Serial.println("FS:  SD card not detected");
    }
    
    Serial.println("\n=== Safe Shutdown ===");
    Serial.println("Use 'shutdown' command for safe poweroff");
    Serial.println("- Checks for unsaved VFS files");
    Serial.println("- Prompts to commit to SD");
    Serial.println("- Verifies FS integrity");
    Serial.println("- Closes all open files");
    
    Serial.println("\n*** ROOT KILL WARNING ***");
    Serial.println("Killing the 'idle' task (ID 0) will");
    Serial.println("DESTROY the kernel and halt execution.");
    
    Serial.println("\nType 'help' for commands");
    
    klog(0, "KERNEL: Boot complete");
    
    shell_prompt();
    
    kernel.running = true;
}

void loop() {
    if (kernel.current_task >= MAX_TASKS) {
        disable_all_interrupts();
        while(1) { 
            __asm__ volatile ("wfi");
        }
    }
    
    if (!kernel.running) {
        disable_all_interrupts();
        while(1) { 
            __asm__ volatile ("wfi");
        }
    }
    
    if (kernel.task_count == 0) {
        disable_all_interrupts();
        Serial.println("NO TASKS - HALT");
        Serial.flush();
        while(1) { 
            __asm__ volatile ("wfi");
        }
    }
    
    uint64_t loop_start = get_time_us();
    
    scheduler_tick();
    
    if (kernel.tasks[0].state == TASK_TERMINATED || kernel.tasks[0].entry == NULL) {
        disable_all_interrupts();
        digitalWrite(LCD_LED, LOW);
        Serial.println("IDLE TASK DEAD - HALT");
        Serial.flush();
        while(1) { 
            __asm__ volatile ("wfi");
        }
    }
    
    TCB* task = &kernel.tasks[kernel.current_task];
    
    if (task->state == TASK_TERMINATED) {
        task_yield();
        return;
    }
    
    if ((task->state == TASK_READY || task->state == TASK_RUNNING) && task->entry) {
        task->state = TASK_RUNNING;
        
        uint64_t task_start = get_time_us();
        task->entry(task->arg);
        uint64_t task_duration = get_time_us() - task_start;
        
        task->cpu_time += task_duration / 1000;
        
        if (task->state == TASK_RUNNING) {
            task->state = TASK_READY;
        }
    }
    
    task_yield();
    
    uint64_t elapsed = get_time_us() - loop_start;
    if (elapsed < SCHEDULER_TICK_US) {
        precise_sleep_us(SCHEDULER_TICK_US - elapsed);
    }
}

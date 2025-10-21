//Picomimi v9.6.1 - Bad Apple!! Edition (Compiler Fix)
//CHANGES FROM v9.6:
//- FIX: Resolved a compilation error caused by a 'memcpy_P' redefinition between the AnimatedGIF library and the RP2040 core headers.
//- FIX: The 'millis'/'delay' error is resolved by asking the user to add '#include <Arduino.h>' to the library's .cpp file.

//NOTE: SD card may not detect after hardware reset (power cycle recommended) - As far as I am aware, not code issue?
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <SD.h>

// This block fixes compatibility issues with the AnimatedGIF library on RP2040
#define LGFX_USE_V1
#undef memcpy_P
#include <AnimatedGIF.h>
#undef memcpy_P // Redefine it again after the include to be safe

#include <hardware/adc.h>
#include <hardware/watchdog.h>
#include <hardware/sync.h>
#include <hardware/flash.h>
#include <pico/platform.h>


#define disable_all_interrupts() __asm__ volatile ("cpsid i" : : : "memory")
#define enable_all_interrupts() __asm__ volatile ("cpsie i" : : : "memory")

// --- Pin Definitions ---
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

// Button pins (shared by kernel and keyboard)
#define BTN_TOP     0
#define BTN_RIGHT   1
#define BTN_BOTTOM  2
#define BTN_LEFT    3
#define BTN_SELECT  4
#define BTN_START   6
#define BTN_A       7
#define BTN_B       8
#define BTN_ONOFF   9 // Used to toggle Keyboard UI

// --- Kernel & System Constants ---
#define LCD_WIDTH   320
#define LCD_HEIGHT  240
#define TERM_COLS   53
#define TERM_ROWS   30
#define TERM_CHAR_W 6
#define TERM_CHAR_H 8

#define SCROLLBACK_ROWS 120 // How many lines of history to keep

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
#define VFS_MAX_BLOCKS_PER_FILE 64  // NEW: Support fragmented files

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
#define FILE_TYPE_DATA 0x03
#define FILE_TYPE_CONFIG  0x04

// --- UI State Management ---
enum UIMode {
    UI_TERMINAL,
    UI_KEYBOARD,
    UI_APPLICATION // New mode for a focused GUI app
};
volatile UIMode current_ui_mode = UI_TERMINAL;
volatile bool ui_mode_changed = true;
// Start with true to force initial draw

// --- NEW: UISocket API Definitions ---
enum UIButton {
    UI_BTN_TOP    = 0,
    UI_BTN_RIGHT  = 1,
    UI_BTN_BOTTOM = 2,
    UI_BTN_LEFT   = 3,
};

// The API "socket" passed to GUI applications
struct UISocket {
    bool (*request_focus)(uint32_t task_id);
    void (*release_focus)(uint32_t task_id);
    bool (*get_button_state)(UIButton button);
    void (*fill_rect)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void (*draw_text)(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size, bool center);
    void (*clear_screen)();
};

// --- Struct Definitions ---
struct ModuleCallbacks {
    void (*init)(uint32_t id);
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

// FIX #3: New VFS structure to support fragmented files
struct VFSBlockChain {
    uint16_t blocks[VFS_MAX_BLOCKS_PER_FILE];
uint8_t block_count;
};

struct VFSFile {
    char name[VFS_FILENAME_LEN];
    uint8_t type;
    bool in_use;
    uint16_t _padding;
// Changed from block_start
    uint32_t size;
    uint32_t created;
    uint32_t modified;
    uint32_t owner_id;
    VFSBlockChain chain;
// NEW: Block allocation chain
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
    uint32_t owner_task_id;  // NEW: Track which task owns this file
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
    
    int32_t gui_focus_task_id; // NEW: Task ID that has GUI control, -1 for none
    
    uint8_t heap[HEAP_SIZE];
};

static KernelState kernel __attribute__((aligned(64)));

static char cmd_buffer[128];
static uint32_t cmd_pos = 0;
static Adafruit_ILI9341 tft = Adafruit_ILI9341(LCD_CS, LCD_DC, LCD_RESET);

// --- Terminal Buffers and State ---
static char term_history[SCROLLBACK_ROWS][TERM_COLS + 1];
static char screen_buffer[TERM_ROWS][TERM_COLS + 2];
static int history_head = -1;
static int history_col = 0;
static int history_count = 0;
static int view_offset = 0;
volatile bool term_dirty = true;
// Flag to signal a redraw is needed

// --- Smooth Scrolling State ---
static uint8_t scroll_hold_state = 0;
static uint32_t next_scroll_time = 0;
const uint32_t SCROLL_INITIAL_DELAY = 400;
const uint32_t SCROLL_REPEAT_DELAY = 80;
// --- Virtual Keyboard State ---
const char* rows[] = {
  "1234567890",
  "qwertyuiop",
  "asdfghjkl",
  "zxcvbnm/:.",
  " <\n"  // Space, backspace, and enter
};
const int NUM_ROWS = 5;
int currentRow = 0;
int currentCol = 0;
int lastRow = -1;
int lastCol = -1;
unsigned long lastButtonTime = 0;
const int DEBOUNCE_DELAY = 150;

// --- Forward Declarations ---
void term_render();
void shell_prompt();
void shell_execute(char* cmd);
void k_register_gui_app(UISocket* socket_api);

// --- UISocket API Forward Declarations ---
bool k_request_gui_focus(uint32_t task_id);
void k_release_gui_focus(uint32_t task_id);

// --- MultiPrint Class ---
class MultiPrint : public Print {
public:
    virtual size_t write(uint8_t c) {
        Serial.write(c);
if (!kernel.display_alive) return 1;

        if (history_head == -1) {
            history_head = 0;
history_count = 1;
            memset(term_history[0], 0, TERM_COLS + 1);
        }

        if (c == '\n') {
            history_col = 0;
history_head = (history_head + 1) % SCROLLBACK_ROWS;
            if (history_count < SCROLLBACK_ROWS) history_count++;
            memset(term_history[history_head], 0, TERM_COLS + 1);
            view_offset = 0;
term_dirty = true;
        } else if (c != '\r') {
            if (history_col < TERM_COLS) {
                term_history[history_head][history_col++] = c;
}
        }
        return 1;
}

    virtual size_t write(const uint8_t *buffer, size_t size) {
        for(size_t i = 0; i < size; i++) {
            write(buffer[i]);
}
        if (size > 0 && kernel.display_alive) term_dirty = true;
        return size;
    }
};
MultiPrint kout;

// --- Virtual Keyboard Functions ---
void keyboard_draw_input_area() {
    tft.fillRect(0, 0, 320, 50, ILI9341_BLACK);
tft.drawRect(5, 5, 310, 40, ILI9341_DARKGREY);
    tft.setTextColor(ILI9341_YELLOW);
    tft.setTextSize(2);
    tft.setCursor(10, 15);
    
    char display_buf[40];
    int len = strlen(cmd_buffer);
if (len < 20) {
        snprintf(display_buf, sizeof(display_buf), "> %s", cmd_buffer);
} else {
        snprintf(display_buf, sizeof(display_buf), "> ...%s", cmd_buffer + len - 17);
}
    tft.print(display_buf);
    tft.print("_");
}

void drawKey(int row, int col, int x, int y, bool selected) {
    char c = rows[row][col];
uint16_t bgColor = selected ? ILI9341_DARKGREY : ILI9341_BLACK;
    uint16_t borderColor = ILI9341_DARKGREY;
    uint16_t textColor = ILI9341_WHITE;

    int w = 28;
int h = 32;
    if (c == ' ') { w = 58;
}
    
    tft.fillRect(x, y, w, h, bgColor);
    tft.drawRect(x, y, w, h, borderColor);
    
    tft.setTextColor(textColor);
if (c == ' ') {
        tft.setTextSize(1);
        tft.setCursor(x + 12, y + 12);
tft.print("SPACE");
    } else if (c == '<') {
        tft.setTextSize(1);
tft.setCursor(x + 6, y + 12);
        tft.print("DEL");
    } else if (c == '\n') {
        tft.setTextSize(1);
tft.setCursor(x + 2, y + 12);
        tft.print("ENTER");
    } else {
        tft.setTextSize(2);
tft.setCursor(x + 8, y + 8);
        tft.print(c);
    }
}

void drawKeyboard() {
    int startY = 60;
int rowHeight = 36;
    for (int r = 0; r < NUM_ROWS; r++) {
        int y = startY + r * rowHeight;
int numKeys = strlen(rows[r]);
        int totalWidth = numKeys * 30;
        int startX = (320 - totalWidth) / 2;
for (int c = 0; c < numKeys; c++) {
            drawKey(r, c, startX + c * 30, y, (r == currentRow && c == currentCol));
}
    }
}

void updateKeyHighlight() {
    int startY = 60;
    int rowHeight = 36;
if (lastRow >= 0 && lastCol >= 0) {
        int numKeys = strlen(rows[lastRow]);
int totalWidth = numKeys * 30;
        int startX = (320 - totalWidth) / 2;
int y = startY + lastRow * rowHeight;
        drawKey(lastRow, lastCol, startX + lastCol * 30, y, false);
}
    
    int numKeys = strlen(rows[currentRow]);
    int totalWidth = numKeys * 30;
int startX = (320 - totalWidth) / 2;
    int y = startY + currentRow * rowHeight;
drawKey(currentRow, currentCol, startX + currentCol * 30, y, true);
    
    lastRow = currentRow;
    lastCol = currentCol;
}

void keyboard_render_full() {
    tft.fillScreen(ILI9341_BLACK);
    keyboard_draw_input_area();
    drawKeyboard();
    lastRow = -1;
    lastCol = -1;
    updateKeyHighlight();
}

void keyboard_handle_input() {
    unsigned long currentTime = millis();
if (currentTime - lastButtonTime < DEBOUNCE_DELAY) {
        return;
}

    if (digitalRead(BTN_A) == LOW) {
        lastButtonTime = currentTime;
        currentCol++;
if (currentCol >= strlen(rows[currentRow])) {
            currentCol = 0;
}
        updateKeyHighlight();
    }

    if (digitalRead(BTN_B) == LOW) {
        lastButtonTime = currentTime;
currentCol--;
        if (currentCol < 0) {
            currentCol = strlen(rows[currentRow]) - 1;
}
        updateKeyHighlight();
    }

    if (digitalRead(BTN_START) == LOW) {
        lastButtonTime = currentTime;
currentRow++;
        if (currentRow >= NUM_ROWS) {
            currentRow = 0;
}
        if (currentCol >= strlen(rows[currentRow])) {
            currentCol = 0;
}
        updateKeyHighlight();
    }

    if (digitalRead(BTN_SELECT) == LOW) {
        lastButtonTime = currentTime;
char c = rows[currentRow][currentCol];

        if (c == '\n') {
            strncat(term_history[history_head], cmd_buffer, TERM_COLS - history_col);
kout.println();
            shell_execute(cmd_buffer);
            cmd_pos = 0;
            memset(cmd_buffer, 0, sizeof(cmd_buffer));
            if (kernel.shell_alive) {
                shell_prompt();
}
            current_ui_mode = UI_TERMINAL;
            ui_mode_changed = true;
} else if (c == '<') {
            if (cmd_pos > 0) {
                cmd_pos--;
cmd_buffer[cmd_pos] = '\0';
                keyboard_draw_input_area();
            }
        } else {
            if (cmd_pos < sizeof(cmd_buffer) - 1) {
                cmd_buffer[cmd_pos++] = c;
cmd_buffer[cmd_pos] = '\0';
                keyboard_draw_input_area();
            }
        }
    }
}


// --- Kernel Functions ---
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
    
    kout.println("[VFS] Initialized (inactive)");
    klog(0, "VFS: Init OK");
}

void vfs_format() {
    if (!kernel.vfs_sb || !kernel.vfs_data) {
        kout.println("[VFS] Not allocated");
return;
    }
    
    kout.println("[VFS] Formatting filesystem...");
    
    memset(kernel.vfs_sb, 0, sizeof(VFSSuperblock));
    memset(kernel.vfs_data, 0xFF, VFS_STORAGE_SIZE);
kernel.vfs_sb->magic = 0x52503230;
    kernel.vfs_sb->version = 2;  // Increment version for fragmented support
    kernel.vfs_sb->total_blocks = VFS_STORAGE_SIZE / VFS_BLOCK_SIZE;
kernel.vfs_sb->free_blocks = kernel.vfs_sb->total_blocks;
    kernel.vfs_sb->file_count = 0;
    
    memset(kernel.vfs_sb->block_bitmap, 0, sizeof(kernel.vfs_sb->block_bitmap));
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        kernel.vfs_sb->files[i].in_use = false;
kernel.vfs_sb->files[i].name[0] = '\0';
        kernel.vfs_sb->files[i].chain.block_count = 0;
    }
    
    kout.println("[VFS] Format complete (fragmented mode)");
klog(0, "VFS: Formatted v2");
}

bool vfs_mount() {
    if (!kernel.vfs_active) {
        kout.println("[VFS] Not active. Use 'vfscreate' first");
return false;
    }
    
    if (!kernel.vfs_sb || !kernel.vfs_data) {
        kout.println("[VFS] Not allocated");
return false;
    }
    
    vfs_format();
    
    kernel.vfs_mounted = true;
    kernel.vfs_alive = true;
    
    kout.println("[VFS] Mounted");
klog(0, "VFS: Mounted");
    
    return true;
}

void vfs_unmount() {
    if (!kernel.vfs_mounted) return;
    
    kernel.vfs_mounted = false;
    kernel.vfs_alive = false;
kout.println("[VFS] Unmounted");
    klog(0, "VFS: Unmounted");
}

int vfs_create(const char* name, uint8_t type, uint32_t owner_id) {
    if (!kernel.vfs_mounted) {
        kout.println("[VFS] Not mounted");
return -1;
    }
    
    if (strlen(name) >= VFS_FILENAME_LEN) {
        kout.println("[VFS] Filename too long");
return -1;
    }
    
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (kernel.vfs_sb->files[i].in_use && 
            strcmp(kernel.vfs_sb->files[i].name, name) == 0) {
            kout.println("[VFS] File exists");
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
        kout.println("[VFS] No free file entries");
return -1;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    strncpy(file->name, name, VFS_FILENAME_LEN - 1);
file->name[VFS_FILENAME_LEN - 1] = '\0';
    file->type = type;
    file->in_use = true;
    file->chain.block_count = 0;
// FIX #3: Initialize block chain
    file->size = 0;
    file->created = get_time_ms();
    file->modified = file->created;
file->owner_id = owner_id;
    
    kernel.vfs_sb->file_count++;
    
    kout.print("[VFS] Created: ");
    kout.println(name);
    
    return fd;
}

// FIX #3: Rewritten to support fragmented allocation
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
// Check if we have enough blocks (can be fragmented)
    if (blocks_needed > kernel.vfs_sb->free_blocks) {
        kout.println("[VFS] Insufficient space");
return -1;
    }
    
    if (blocks_needed > VFS_MAX_BLOCKS_PER_FILE) {
        kout.println("[VFS] File too large for block chain");
return -1;
    }
    
    // Find ANY free blocks (non-contiguous is OK)
    uint16_t allocated_blocks[VFS_MAX_BLOCKS_PER_FILE];
uint32_t allocated_count = 0;
    
    for (uint32_t i = 0; i < kernel.vfs_sb->total_blocks && allocated_count < blocks_needed; i++) {
        uint32_t byte_idx = i / 8;
uint32_t bit_idx = i % 8;
        
        // Check if block is free
        if (!(kernel.vfs_sb->block_bitmap[byte_idx] & (1 << bit_idx))) {
            allocated_blocks[allocated_count++] = i;
}
    }
    
    if (allocated_count < blocks_needed) {
        kout.println("[VFS] Block allocation failed");
return -1;
    }
    
    // Mark blocks as used and write data
    file->chain.block_count = 0;
uint32_t bytes_written = 0;
    
    for (uint32_t i = 0; i < allocated_count; i++) {
        uint16_t block_num = allocated_blocks[i];
// Mark block as used
        uint32_t byte_idx = block_num / 8;
uint32_t bit_idx = block_num % 8;
        kernel.vfs_sb->block_bitmap[byte_idx] |= (1 << bit_idx);
        kernel.vfs_sb->free_blocks--;
// Add to file's block chain
        file->chain.blocks[file->chain.block_count++] = block_num;
// Write data to this block
        uint32_t bytes_to_write = min((uint32_t)VFS_BLOCK_SIZE, size - bytes_written);
uint32_t offset = block_num * VFS_BLOCK_SIZE;
        memcpy(kernel.vfs_data + offset, (uint8_t*)data + bytes_written, bytes_to_write);
        bytes_written += bytes_to_write;
}
    
    file->size = size;
    file->modified = get_time_ms();
    kernel.vfs_writes++;
    
    return size;
}

// FIX #3: Updated to read from fragmented blocks
int vfs_read(int fd, void* buffer, uint32_t size) {
    if (!kernel.vfs_mounted || fd < 0 || fd >= VFS_MAX_FILES) {
        return -1;
}
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
if (!file->in_use || file->chain.block_count == 0) {
        return -1;
}
    
    uint32_t read_size = size < file->size ? size : file->size;
// Read from fragmented blocks
    uint32_t bytes_read = 0;
for (uint8_t i = 0; i < file->chain.block_count && bytes_read < read_size; i++) {
        uint16_t block_num = file->chain.blocks[i];
uint32_t offset = block_num * VFS_BLOCK_SIZE;
        uint32_t bytes_to_read = min((uint32_t)VFS_BLOCK_SIZE, read_size - bytes_read);
        
        memcpy((uint8_t*)buffer + bytes_read, kernel.vfs_data + offset, bytes_to_read);
bytes_read += bytes_to_read;
    }
    
    kernel.vfs_reads++;
    return bytes_read;
}

void vfs_delete(int fd) {
    if (!kernel.vfs_mounted || fd < 0 || fd >= VFS_MAX_FILES) {
        return;
}
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
    if (!file->in_use) return;
// FIX #3: Free all blocks in the chain
    for (uint8_t i = 0; i < file->chain.block_count; i++) {
        uint16_t block_num = file->chain.blocks[i];
uint32_t byte_idx = block_num / 8;
        uint32_t bit_idx = block_num % 8;
        kernel.vfs_sb->block_bitmap[byte_idx] &= ~(1 << bit_idx);
        kernel.vfs_sb->free_blocks++;
}
    
    file->in_use = false;
    file->chain.block_count = 0;
    kernel.vfs_sb->file_count--;
    
    kout.print("[VFS] Deleted: ");
    kout.println(file->name);
}

void vfs_list() {
    if (!kernel.vfs_mounted) {
        kout.println("[VFS] Not mounted");
        return;
}
    
    kout.println("\n=== VFS Contents ===");
kout.println("ID  Name             Type   Size    Blks  Owner");
kout.println("--  ---------------  -----  ------  ----  -----");
    
    const char* type_str[] = {"", "TEXT", "LOG", "DATA", "CONF"};
for (int i = 0; i < VFS_MAX_FILES; i++) {
        VFSFile* file = &kernel.vfs_sb->files[i];
if (file->in_use) {
            char buf[80];
snprintf(buf, sizeof(buf), "%2d  %-15s  %-5s  %6d  %4d  %5d",
                     i, file->name, 
                     file->type < 5 ? type_str[file->type] : "?",
                     file->size, file->chain.block_count, file->owner_id);
kout.println(buf);
        }
    }
    
    kout.print("\nFiles: ");
    kout.print(kernel.vfs_sb->file_count);
    kout.print("/");
    kout.println(VFS_MAX_FILES);
}

void vfs_stats() {
    if (!kernel.vfs_mounted) {
        kout.println("[VFS] Not mounted");
        return;
}
    
    kout.println("\n=== VFS Statistics ===");
    kout.print("Total blocks:  "); kout.println(kernel.vfs_sb->total_blocks);
kout.print("Free blocks:   "); kout.println(kernel.vfs_sb->free_blocks);
    kout.print("Files:         "); kout.print(kernel.vfs_sb->file_count);
    kout.print("/"); kout.println(VFS_MAX_FILES);
kout.print("Total writes:  "); kout.println(kernel.vfs_writes);
    kout.print("Total reads:   "); kout.println(kernel.vfs_reads);
kout.println("Mode:          Fragmented allocation");
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
kernel.fs_open_files[i].owner_task_id = 0;  // FIX #2: Initialize owner
    }
    
    kout.println("[FS] Initializing SD card...");
kout.println("[FS] Waiting for card to stabilize...");
    
    // Ensure CS is high and wait for card power-up
    pinMode(SD_CS, OUTPUT);
digitalWrite(SD_CS, HIGH);
    delay(1000);  // Give SD card time to power up after reset
    
    kout.println("[FS] Attempting connection at 400kHz...");
// Single slow init attempt
    if (SD.begin(SD_CS, 400000)) {
        kout.println("[FS] SD card detected!");
kernel.fs_available = true;
    } else {
        kout.println("[FS] SD card not detected");
kout.println("[FS] Note: Press reset again if card exists");
        klog(1, "FS: No SD card");
        return;
}
    
    kout.println("[FS] Card initialized, detecting size...");
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
        
        kout.print("[FS] Estimated size: ");
        kout.print((uint32_t)(kernel.fs_total_bytes / (1024 * 1024)));
kout.println(" MB (max 4GB)");
        
        kout.print("[FS] Used: ");
        kout.print((uint32_t)(kernel.fs_used_bytes / (1024 * 1024)));
        kout.println(" MB");
} else {
        kernel.fs_total_bytes = FS_MAX_CARD_SIZE;
        kernel.fs_used_bytes = 0;
        kout.println("[FS] Size detection skipped");
}
    
    klog(0, "FS: Init OK");
}

void fs_log_init() {
    if (!kernel.fs_available) return;
    
    File logFile = SD.open(FS_LOG_FILE, FILE_READ);
if (!logFile) {
        kout.println("[FS] Creating LogRecord file");
        logFile = SD.open(FS_LOG_FILE, FILE_WRITE);
if (logFile) {
            logFile.println("=== RP2040 Kernel Error Log ===");
logFile.println("Format: [N] Timestamp Message");
            logFile.println("================================");
            logFile.close();
            kernel.fs_log_counter = 0;
            kout.println("[FS] LogRecord created");
} else {
            kout.println("[FS] Failed to create LogRecord");
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
kout.print("[FS] LogRecord found, last entry: ");
        kout.println(kernel.fs_log_counter);
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
        kout.println("[FS] SD card unavailable");
return false;
    }
    
    kernel.fs_mounted = true;
    kernel.fs_alive = true;
    
    fs_log_init();
    
    kout.println("[FS] Mounted");
klog(0, "FS: Mounted");
    
    return true;
}

void fs_unmount() {
    if (!kernel.fs_mounted) return;
for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
        if (kernel.fs_open_files[i].open) {
            kernel.fs_open_files[i].handle.close();
kernel.fs_open_files[i].open = false;
            kernel.fs_open_files[i].owner_task_id = 0;  // FIX #2
        }
    }
    
    kernel.fs_mounted = false;
kernel.fs_alive = false;
    
    kout.println("[FS] Unmounted");
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
        kout.println("[FS] Not mounted");
return;
    }
    
    File root = SD.open(path);
if (!root) {
        kout.println("[FS] Failed to open directory");
        return;
}
    
    if (!root.isDirectory()) {
        kout.println("[FS] Not a directory");
root.close();
        return;
    }
    
    kout.println("\n=== FS Contents ===");
kout.println("Name                             Type   Size");
kout.println("-------------------------------  -----  --------");
    
    File file = root.openNextFile();
    while (file) {
        char buf[80];
snprintf(buf, sizeof(buf), "%-31s  %-5s  %8d",
                 file.name(),
                 file.isDirectory() ? "DIR" : "FILE",
                 (int)file.size());
kout.println(buf);
        file.close();
        file = root.openNextFile();
    }
    
    root.close();
}

void fs_stats() {
    if (!kernel.fs_mounted) {
        kout.println("[FS] Not mounted");
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
    
    kout.println("\n=== FS Statistics ===");
kout.print("Total space:   "); 
    kout.print((uint32_t)(kernel.fs_total_bytes / (1024 * 1024))); 
    kout.println(" MB (est)");
    
    kout.print("Used space:    ");
kout.print((uint32_t)(used / (1024 * 1024))); 
    kout.println(" MB");
    
    kout.print("Free space:    "); 
    kout.print((uint32_t)((kernel.fs_total_bytes - used) / (1024 * 1024)));
kout.println(" MB (est)");
    
    kout.print("Total reads:   "); kout.println(kernel.fs_reads);
    kout.print("Total writes:  "); kout.println(kernel.fs_writes);
}

// FIX #2: Track file owner for cleanup
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
        kout.println("[FS] No free file handles");
return -1;
    }
    
    File file;
if (write_mode) {
        file = SD.open(path, FILE_WRITE);
} else {
        file = SD.open(path, FILE_READ);
}
    
    if (!file) {
        kout.println("[FS] Failed to open file");
return -1;
    }
    
    kernel.fs_open_files[fd].handle = file;
    kernel.fs_open_files[fd].open = true;
    kernel.fs_open_files[fd].write_mode = write_mode;
kernel.fs_open_files[fd].owner_task_id = kernel.current_task;  // FIX #2: Track owner
    strncpy(kernel.fs_open_files[fd].path, path, FS_MAX_FILENAME - 1);
kernel.fs_open_files[fd].path[FS_MAX_FILENAME - 1] = '\0';
    
    return fd;
}

void fs_close(int fd) {
    if (fd < 0 || fd >= FS_MAX_OPEN_FILES) return;
if (!kernel.fs_open_files[fd].open) return;
    
    kernel.fs_open_files[fd].handle.close();
    kernel.fs_open_files[fd].open = false;
    kernel.fs_open_files[fd].owner_task_id = 0;
// FIX #2
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
        kout.println("[FS] Not mounted");
return;
    }
    
    File file = SD.open(path, FILE_READ);
if (!file) {
        kout.println("[FS] Failed to open file");
        return;
}
    
    kout.println("\n=== File Contents ===");
while (file.available()) {
        kout.write(file.read());
    }
    kout.println("\n=== End ===");
    
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
                (uint8_t*)kernel.mem_blocks[i + 1].addr) {
                
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
    kout.println("\n!!! OUT OF MEMORY !!!");
klog(3, "OOM: Out of memory!");
    
    kout.println("OOM: Attempting memory compaction...");
    mem_compact();
    calculate_fragmentation();
if (kernel.largest_free_block > 4096) {
        kout.println("OOM: Compaction successful");
        klog(1, "OOM: Compaction resolved crisis");
return;
    }
    
    kout.println("OOM: Selecting APPLICATION victim...");
    
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
kout.print("OOM: Killing APPLICATION '");
        kout.print(victim->name);
        kout.print("' (");
        kout.print(max_mem / 1024);
        kout.println(" KB)");
char buf[64];
        snprintf(buf, sizeof(buf), "OOM: Killed %s (%dKB)", victim->name, max_mem / 1024);
        klog(2, buf);
        
        brutal_task_kill(victim_id);
        kernel.oom_kills++;
} else {
        kout.println("OOM: NO KILLABLE APPLICATIONS!");
        kout.println("*** SYSTEM PANIC ***");
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

// --- UISocket API Implementation ---

/**
 * @brief (UISOCKET) Allows a task to request exclusive GUI control.
 * Implements the "one GUI app at a time" rule by killing any existing GUI app.
 * @param task_id The ID of the task requesting focus.
 * @return true if focus was granted, false otherwise.
 */
bool k_request_gui_focus(uint32_t task_id) {
    uint32_t irq_state = save_and_disable_interrupts();

    if (kernel.gui_focus_task_id != -1 && kernel.gui_focus_task_id != task_id) {
        uint32_t old_task_id = kernel.gui_focus_task_id;
        
        if (old_task_id < kernel.task_count && kernel.tasks[old_task_id].state != TASK_TERMINATED) {
            kout.print("\n[GUI] Closing '");
            kout.print(kernel.tasks[old_task_id].name);
            kout.println("' to open new app.");
            
            restore_interrupts(irq_state);
            brutal_task_kill(old_task_id);
            irq_state = save_and_disable_interrupts();
        }
    }

    kernel.gui_focus_task_id = task_id;
    
    current_ui_mode = UI_APPLICATION;
    ui_mode_changed = true;

    restore_interrupts(irq_state);
    return true;
}

/**
 * @brief (UISOCKET) Releases exclusive GUI control.
 * @param task_id The ID of the task releasing focus.
 */
void k_release_gui_focus(uint32_t task_id) {
    uint32_t irq_state = save_and_disable_interrupts();
    
    if (kernel.gui_focus_task_id == task_id) {
        kernel.gui_focus_task_id = -1;
        
        if (current_ui_mode == UI_APPLICATION) {
            current_ui_mode = UI_TERMINAL;
            ui_mode_changed = true;
        }
    }
    
    restore_interrupts(irq_state);
}

// NEW function to read button state for apps
bool k_ui_get_button_state(UIButton button) {
    uint8_t pin = 0;
    switch (button) {
        case UI_BTN_TOP:    pin = BTN_TOP;    break;
        case UI_BTN_RIGHT:  pin = BTN_RIGHT;  break;
        case UI_BTN_BOTTOM: pin = BTN_BOTTOM; break;
        case UI_BTN_LEFT:   pin = BTN_LEFT;   break;
        default: return false;
    }
    return gpio_read_fast(pin);
}

// NEW drawing wrappers for apps to use
void k_ui_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (kernel.gui_focus_task_id == kernel.current_task) {
        tft.fillRect(x, y, w, h, color);
    }
}

void k_ui_draw_text(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size, bool center) {
    if (kernel.gui_focus_task_id == kernel.current_task) {
        tft.setTextSize(size);
        tft.setTextColor(color);
        if (center) {
            int16_t x1, y1;
            uint16_t w, h;
            tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
            tft.setCursor(x - (w/2), y - (h/2));
        } else {
            tft.setCursor(x, y);
        }
        tft.print(text);
    }
}

void k_ui_clear_screen() {
    if (kernel.gui_focus_task_id == kernel.current_task) {
        tft.fillScreen(ILI9341_BLACK);
    }
}

// NEW function to populate the UISocket struct for an app
void k_register_gui_app(UISocket* socket_api) {
    if (!socket_api) return;
    socket_api->request_focus    = k_request_gui_focus;
    socket_api->release_focus    = k_release_gui_focus;
    socket_api->get_button_state = k_ui_get_button_state;
    socket_api->fill_rect        = k_ui_fill_rect;
    socket_api->draw_text        = k_ui_draw_text;
    socket_api->clear_screen     = k_ui_clear_screen;
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
    kernel.gui_focus_task_id = -1; // NEW: Initialize GUI focus
}

uint32_t task_create(const char* name, void (*entry)(void*), void* arg, 
                     uint8_t priority, uint8_t task_type, uint32_t flags,
                     uint64_t max_runtime_ms, uint8_t oom_priority,
                     uint32_t mem_limit, ModuleCallbacks* callbacks,
                  
   const char* description) {
    
    if (kernel.task_count >= MAX_TASKS) {
        kout.println("ERROR: Maximum tasks reached!");
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
        callbacks->init(id);
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
                kout.print("[RESPAWN] '");
kout.print(task->name);
                kout.print("' #");
                kout.println(task->respawn_count + 1);
                
                task->state = TASK_READY;
                task->start_time = now;
                task->last_respawn = now;
                task->respawn_count++;
                task->mem_used = 0;
task->cpu_time = 0;
                task->page_faults = 0;
                
                if (task->callbacks && task->callbacks->init) {
                    task->callbacks->init(task->id);
}
                
                char buf[64];
snprintf(buf, sizeof(buf), "RESPAWN: %s #%d", task->name, task->respawn_count);
                klog(0, buf);
            }
        }
    }
}

// FIX #1: Priority-based scheduler
void task_yield() {
    // Find highest priority READY task
    int best_task = -1;
uint8_t highest_priority = 0;
    
    // Start search from next task (for fairness among same-priority tasks)
    uint32_t start = (kernel.current_task + 1) % kernel.task_count;
for (uint32_t i = 0; i < kernel.task_count; i++) {
        uint32_t idx = (start + i) % kernel.task_count;
TCB* candidate = &kernel.tasks[idx];
        
        if (candidate->state == TASK_TERMINATED) continue;
        
        if (candidate->state == TASK_READY || candidate->state == TASK_RUNNING) {
            // Higher priority value = higher priority
            if (best_task == -1 || candidate->priority > highest_priority) {
                best_task = idx;
highest_priority = candidate->priority;
            }
        }
    }
    
    // If we found a runnable task, switch to it.
// Otherwise, stay on the current task (e.g. idle)
    if (best_task >= 0) {
        kernel.current_task = best_task;
kernel.total_context_switches++;
        kernel.tasks[best_task].context_switches++;
    }
}

// FIX #2: Close all open file handles owned by the task
void brutal_task_kill(uint32_t id) {
    if (id >= kernel.task_count) return;
TCB* task = &kernel.tasks[id];
    if (task->state == TASK_TERMINATED) return;
    
    kout.print("[KILL] '");
    kout.print(task->name);
    kout.println("'");
if (task->task_type == TASK_TYPE_KERNEL) {
        kout.println("\n!!!!! KERNEL KILLED !!!!!");
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
    
    // NEW: If the task being killed has GUI focus, release it.
    if (kernel.gui_focus_task_id == id) {
        k_release_gui_focus(id);
    }
    
    // FIX #2: Close all file handles owned by this task
    uint32_t files_closed = 0;
for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
        if (kernel.fs_open_files[i].open && 
            kernel.fs_open_files[i].owner_task_id == id) {
            
            kout.print("  > Closing file: ");
kout.println(kernel.fs_open_files[i].path);
            
            kernel.fs_open_files[i].handle.close();
            kernel.fs_open_files[i].open = false;
            kernel.fs_open_files[i].owner_task_id = 0;
            files_closed++;
        }
    }
    
    if (files_closed > 0) {
        kout.print("  > Closed ");
kout.print(files_closed);
        kout.println(" file(s)");
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
        kout.print("  > Freed ");
kout.print(freed);
        kout.println(" bytes");
    }
    
    if (strcmp(task->name, "shell") == 0) {
        kernel.shell_alive = false;
kout.println("\n*** SHELL DEAD - NO MORE COMMANDS ***");
    }
    else if (strcmp(task->name, "display") == 0) {
        kernel.display_alive = false;
kout.println("\n*** DISPLAY DRIVER DEAD ***");
    }
    else if (strcmp(task->name, "input") == 0) {
        kernel.input_alive = false;
kout.println("\n*** INPUT DRIVER DEAD ***");
    }
    else if (strcmp(task->name, "cpumon") == 0) {
        kernel.cpumon_alive = false;
}
    else if (strcmp(task->name, "tempmon") == 0) {
        kernel.tempmon_alive = false;
}
    else if (strcmp(task->name, "vfs") == 0) {
        kernel.vfs_alive = false;
kout.println("\n*** VFS DEAD ***");
    }
    else if (strcmp(task->name, "fs") == 0) {
        kernel.fs_alive = false;
kout.println("\n*** FS DEAD ***");
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
    kout.println("\n=== System Commands ===");
kout.println("  help       - Show this help");
kout.println("  ps         - List all tasks");
kout.println("  listapps   - List only applications");
    kout.println("  listmods   - List only modules");
kout.println("  top        - Live system monitor");
kout.println("  mem        - Memory statistics");
kout.println("  memmap     - Detailed memory map");
kout.println("  compact    - Force memory compaction");
kout.println("  dmesg      - Show system log");
kout.println("  uptime     - System uptime");
kout.println("  temp       - CPU temperature");
kout.println("  clear      - Clear screen");
kout.println("  reboot     - Restart system");
    kout.println("  shutdown   - Safe shutdown");
kout.println("\n=== VFS Commands (RAM) ===");
    kout.println("  vfscreate  - Create and mount VFS");
kout.println("  vfsls      - List VFS files");
kout.println("  vfsstat    - VFS statistics");
    kout.println("  vfsmkfile <name> <type> - Create VFS file");
kout.println("  vfsrm <id>    - Delete VFS file");
kout.println("  vfscat <id>   - Read VFS file");
kout.println("  vfsdedicate   - Save all VFS files to SD");
    kout.println("\n=== FS Commands (SD Card) ===");
kout.println("  ls [path]  - List SD files");
    kout.println("  stat       - FS statistics");
kout.println("  mkdir <path> - Create directory");
    kout.println("  rm <path>    - Delete file");
kout.println("  cat <path>   - Read file");
    kout.println("  write <path> <text> - Write text to file");
kout.println("  logcat     - View error log from SD");
    kout.println("\n=== Task Management ===");
kout.println("  kill <id>      - Kill task (apps only)");
kout.println("  root           - Toggle root mode");
kout.println("  root kill <id> - Force kill any task");
    kout.println("  suspend <id>   - Suspend task");
kout.println("  resume <id>    - Resume task");
    kout.println("\n=== Applications ===");
kout.println("  apple      - Play Bad Apple!! GIF (GUI)");
kout.println("  snake      - Snake game (GUI)");
kout.println("  calc       - Calculator");
kout.println("  clock      - Digital clock");
kout.println("  sysmon     - System monitor");
kout.println("  memhog     - Memory stress test");
kout.println("  cpuburn    - CPU stress test");
kout.println("  stress     - Full system stress");
}

void cmd_arch() {
    kout.println("\n=== RP2040 Kernel Task Architecture ===");
    kout.println("\nTASK TYPES:");
kout.println("  1. KERNEL   - Core system (idle)");
    kout.println("  2. DRIVER   - Hardware (display, input)");
kout.println("  3. SERVICE  - System services (shell, vfs, fs)");
kout.println("  4. MODULE   - Extensions (counter, watchdog)");
    kout.println("  5. APPLICATION - User programs (games)");
kout.println("\nGUI SYSTEM (v9.0):");
kout.println("  - Apps register via k_register_gui_app()");
kout.println("  - Request focus via UISocket API");
kout.println("  - Only one GUI app can have focus");
kout.println("\nONLY APPLICATIONS can be OOM killed!");
}

void cmd_oom() {
    kout.println("\n=== OOM Killer ===");
    kout.println("ONLY kills APPLICATIONS");
    kout.println("Priority: 0=Never 1=Crit 2=High 3=Norm 4=Low");
kout.println("Modules/Drivers/Services are PROTECTED");
}

void cmd_ps() {
    kout.println("\nID  Name                 Type      State     Pri OOM  Mem(B)  Peak(B)");
kout.println("--- -------------------- --------- --------- --- ---  ------- --------");
    
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
kout.println(buf);
    }
    
    kout.print("\nSummary: K=");
    kout.print(kernel.kernel_tasks);
    kout.print(" D=");
    kout.print(kernel.driver_tasks);
    kout.print(" S=");
    kout.print(kernel.service_tasks);
    kout.print(" M=");
kout.print(kernel.module_tasks);
    kout.print(" A=");
    kout.println(kernel.application_tasks);
}

void cmd_listapps() {
    kout.println("\n=== Running Applications ===");
    uint32_t count = 0;
for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
if (task->task_type == TASK_TYPE_APPLICATION) {
            kout.print(task->id);
            kout.print(". ");
            kout.print(task->name);
kout.print(" [OOM=");
            kout.print(task->oom_priority);
            kout.println("]");
            count++;
        }
    }
    if (count == 0) kout.println("No applications running");
}

void cmd_listmods() {
    kout.println("\n=== Loaded Modules ===");
    uint32_t count = 0;
for (uint32_t i = 0; i < kernel.task_count; i++) {
        TCB* task = &kernel.tasks[i];
if (task->task_type == TASK_TYPE_MODULE) {
            kout.print(task->id);
            kout.print(". ");
            kout.println(task->name);
count++;
        }
    }
    if (count == 0) kout.println("No modules loaded");
}

void cmd_mem() {
    uint32_t total = HEAP_SIZE;
    uint32_t used = get_used_memory();
    uint32_t free = get_free_memory();
kout.println("\n=== Memory Statistics ===");
    kout.print("  Total:        "); kout.print(total / 1024); kout.println(" KB");
kout.print("  Used:         "); kout.print(used / 1024);
kout.print(" KB (");
    kout.print((used * 100) / total); kout.println("%)");
kout.print("  Free:         "); kout.print(free / 1024); kout.println(" KB");
kout.print("  Largest free: "); kout.print(kernel.largest_free_block / 1024); kout.println(" KB");
    kout.print("  Fragmentation:"); kout.print(kernel.fragmentation_pct); kout.println("%");
    kout.print("  Total blocks: ");
kout.println(kernel.mem_block_count);
    kout.print("  OOM kills:    "); kout.println(kernel.oom_kills);
}

void cmd_memmap() {
    kout.println("\n=== Memory Map (First 20 blocks) ===");
    uint32_t limit = kernel.mem_block_count < 20 ?
kernel.mem_block_count : 20;
    
    for (uint32_t i = 0; i < limit; i++) {
        MemBlock* block = &kernel.mem_blocks[i];
kout.print(i);
        kout.print(". ");
        kout.print(block->free ? "FREE " : "USED ");
        kout.print(block->size);
        kout.print("B owner=");
        kout.println(block->owner_id);
}
}

void cmd_compact() {
    kout.println("[COMPACT] Forcing memory compaction...");
    uint32_t before = kernel.mem_block_count;
    mem_compact();
    uint32_t after = kernel.mem_block_count;
kout.print("Blocks: ");
    kout.print(before);
    kout.print(" -> ");
    kout.println(after);
}

void cmd_dmesg() {
    kout.println("\n=== System Log ===");
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
kout.println(buf);
    }
}

void cmd_top() {
    kout.println("\n=== System Resources ===");
    kout.print("CPU:    "); kout.print(kernel.cpu_usage, 1); kout.println("%");
kout.print("Memory: "); kout.print(get_used_memory() / 1024); kout.print("/");
    kout.print(HEAP_SIZE / 1024); kout.println(" KB");
    kout.print("Temp:   "); kout.print(kernel.temperature, 1); kout.println("C");
    kout.print("Uptime: ");
kout.print(kernel.uptime_ms / 1000); kout.println("s");
}

void cmd_uptime() {
    uint64_t uptime = kernel.uptime_ms;
uint32_t days = uptime / (1000ULL * 60 * 60 * 24);
uint32_t hours = (uptime / (1000ULL * 60 * 60)) % 24;
uint32_t mins = (uptime / (1000ULL * 60)) % 60;
    uint32_t secs = (uptime / 1000ULL) % 60;
    
    kout.print("Uptime: ");
kout.print(days);
    kout.print("d ");
    kout.print(hours);
    kout.print("h ");
    kout.print(mins);
    kout.print("m ");
    kout.print(secs);
    kout.println("s");
}

void cmd_temp() {
    kout.print("CPU Temperature: ");
kout.print(kernel.temperature, 1);
    kout.println("C");
}

void cmd_suspend(uint32_t id) {
    if (id >= kernel.task_count) {
        kout.println("ERROR: Invalid task ID");
return;
    }
    
    TCB* task = &kernel.tasks[id];
if (task->state == TASK_TERMINATED) {
        kout.println("ERROR: Task is terminated");
        return;
}
    
    if (task->task_type != TASK_TYPE_APPLICATION) {
        kout.println("ERROR: Can only suspend applications");
return;
    }
    
    task->state = TASK_SUSPENDED;
    kout.print("Task '");
    kout.print(task->name);
    kout.println("' suspended");
}

void cmd_resume(uint32_t id) {
    if (id >= kernel.task_count) {
        kout.println("ERROR: Invalid task ID");
return;
    }
    
    TCB* task = &kernel.tasks[id];
if (task->state != TASK_SUSPENDED) {
        kout.println("ERROR: Task is not suspended");
        return;
}
    
    task->state = TASK_READY;
    kout.print("Task '");
    kout.print(task->name);
    kout.println("' resumed");
}

void cmd_kill(uint32_t id, bool force) {
    if (id >= kernel.task_count) {
        kout.println("ERROR: Invalid task ID");
return;
    }
    
    TCB* task = &kernel.tasks[id];
if (task->state == TASK_TERMINATED) {
        kout.println("ERROR: Task already terminated");
        return;
}
    
    if (task->task_type != TASK_TYPE_APPLICATION && !force) {
        kout.println("ERROR: Cannot kill system task");
kout.println("Use 'root kill <id>' to force");
        return;
    }
    
    if ((task->flags & TASK_FLAG_PROTECTED) && !force) {
        kout.println("ERROR: Task is protected");
kout.println("Use 'root kill <id>' to force");
        return;
    }
    
    if (force && task->task_type != TASK_TYPE_APPLICATION) {
        kout.println("*** WARNING: Killing system component! ***");
}
    
    brutal_task_kill(id);
    
    if (force) {
        kernel.root_mode = false;
kout.println("[Root mode auto-disabled]");
    }
}

void cmd_root() {
    kernel.root_mode = !kernel.root_mode;
if (kernel.root_mode) {
        kout.println("\n*** ROOT MODE ENABLED ***");
kout.println("WARNING: Can now kill protected tasks!");
    } else {
        kout.println("Root mode disabled");
}
}

void cmd_reboot() {
    kout.println("\n*** SYSTEM REBOOT ***");
    kout.println("Rebooting...");
    term_render();
    delay(1000);
    watchdog_enable(1, 1);
    while(1);
}

void cmd_shutdown() {
    kout.println("\n*** INITIATING SAFE SHUTDOWN ***");
if (kernel.vfs_mounted && kernel.vfs_sb->file_count > 0) {
        kout.print("VFS has ");
        kout.print(kernel.vfs_sb->file_count);
kout.println(" unsaved files.");
        kout.print("Commit to SD? (y/n): ");
        term_render();
        
        unsigned long timeout = millis() + 10000;
while (millis() < timeout) {
            if (Serial.available()) {
                char c = Serial.read();
kout.println(c);
                if (c == 'y' || c == 'Y') {
                    kout.println("Committing VFS to SD...");
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
kout.print("  Saved: ");
                                    kout.println(path);
                                }
                            }
                        }
                        kout.println("VFS committed to SD");
} else {
                        kout.println("ERROR: FS not mounted");
}
                    break;
} else if (c == 'n' || c == 'N') {
                    kout.println("VFS files discarded");
break;
                }
            }
        }
    }
    
    kout.println("Checking FS integrity...");
if (kernel.fs_mounted) {
        for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
            if (kernel.fs_open_files[i].open) {
                kout.print("  Closing: ");
kout.println(kernel.fs_open_files[i].path);
                fs_close(i);
            }
        }
        kout.println("All files closed");
}
    
    kout.println("Flushing logs...");
    term_render();
    Serial.flush();
    delay(500);
    
    kout.println("Stopping services...");
    if (kernel.vfs_mounted) vfs_unmount();
if (kernel.fs_mounted) fs_unmount();
    
    kout.println("*** SHUTDOWN COMPLETE ***");
    kout.println("Safe to power off");
    term_render();
    Serial.flush();
    
    kernel.running = false;
while(1) {
        __asm__ volatile ("wfi");
}
}

void cmd_vfscreate() {
    if (kernel.vfs_active) {
        kout.println("VFS already active");
        return;
}
    
    kernel.vfs_sb = (VFSSuperblock*)kmalloc(sizeof(VFSSuperblock), 0);
if (!kernel.vfs_sb) {
        kout.println("[VFS] Failed to allocate superblock");
        return;
}
    
    kernel.vfs_data = (uint8_t*)kmalloc(VFS_STORAGE_SIZE, 0);
if (!kernel.vfs_data) {
        kfree(kernel.vfs_sb);
        kernel.vfs_sb = NULL;
        kout.println("[VFS] Failed to allocate data buffer");
return;
    }
    
    kernel.vfs_active = true;
    vfs_mount();
}

void cmd_vfsdedicate() {
    if (!kernel.vfs_mounted) {
        kout.println("ERROR: VFS not mounted");
return;
    }
    
    if (!kernel.fs_mounted) {
        kout.println("ERROR: FS not mounted");
return;
    }
    
    if (kernel.vfs_sb->file_count == 0) {
        kout.println("VFS is empty");
return;
    }
    
    kout.println("Saving VFS files to SD...");
    
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
kout.print("  Saved: ");
                kout.println(path);
            }
        }
    }
    
    kout.print("Dedicated ");
kout.print(saved);
    kout.println(" files to SD");
}

void cmd_vfsmkfile(char* name, uint8_t type) {
    if (!kernel.vfs_mounted) {
        kout.println("ERROR: VFS not mounted. Use 'vfscreate' first");
return;
    }
    
    int fd = vfs_create(name, type, kernel.current_task);
if (fd >= 0) {
        kout.print("Created VFS file: ");
        kout.print(name);
kout.print(" (fd=");
        kout.print(fd);
        kout.println(")");
        
        char sample[64];
snprintf(sample, sizeof(sample), "Sample data for %s\n", name);
        int written = vfs_write(fd, sample, strlen(sample));
if (written > 0) {
            kout.print("Wrote ");
            kout.print(written);
kout.println(" bytes");
        }
    }
}

void cmd_vfsrm(int fd) {
    if (!kernel.vfs_mounted) {
        kout.println("ERROR: VFS not mounted");
return;
    }
    vfs_delete(fd);
}

void cmd_vfscat(int fd) {
    if (!kernel.vfs_mounted) {
        kout.println("ERROR: VFS not mounted");
return;
    }
    
    if (fd < 0 || fd >= VFS_MAX_FILES) {
        kout.println("ERROR: Invalid file descriptor");
return;
    }
    
    VFSFile* file = &kernel.vfs_sb->files[fd];
if (!file->in_use) {
        kout.println("ERROR: File not in use");
        return;
}
    
    char buffer[512];
    int bytes = vfs_read(fd, buffer, sizeof(buffer) - 1);
if (bytes > 0) {
        buffer[bytes] = '\0';
        kout.println("\n=== VFS File Contents ===");
kout.println(buffer);
        kout.println("=== End ===");
    } else {
        kout.println("ERROR: Read failed or empty file");
}
}

void cmd_write(char* path, char* text) {
    if (!kernel.fs_mounted) {
        kout.println("ERROR: FS not mounted");
return;
    }
    
    int fd = fs_open(path, true);
if (fd < 0) {
        kout.println("ERROR: Failed to open file");
        return;
}
    
    int written = fs_write_str(fd, text);
    fs_write_str(fd, "\n");
    fs_close(fd);
    
    kout.print("Wrote ");
    kout.print(written);
kout.print(" bytes to ");
    kout.println(path);
}

void shell_execute(char* cmd) {
    if (strlen(cmd) == 0) return;
if (strncmp(cmd, "root kill ", 10) == 0) {
        if (!kernel.root_mode) {
            kout.println("ERROR: Root mode not enabled");
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
        if (kernel.display_alive) {
            history_head = -1;
history_col = 0;
            history_count = 0;
            view_offset = 0;
            memset(screen_buffer, 1, sizeof(screen_buffer));
            term_dirty = true;
}
    }
    else if (strcmp(cmd, "snake") == 0) {
        extern void snake_spawn();
snake_spawn();
    }
    else if (strcmp(cmd, "apple") == 0) {
        extern void apple_spawn();
apple_spawn();
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
            kout.println("Usage: vfsmkfile <name> <type>");
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
            kout.println("Directory created");
} else {
            kout.println("Failed to create directory");
}
    }
    else if (strncmp(cmd, "rm ", 3) == 0) {
        if (fs_remove(cmd + 3)) {
            kout.println("File deleted");
} else {
            kout.println("Failed to delete file");
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
            kout.println("Usage: write <path> <text>");
}
    }
    else if (strcmp(cmd, "logcat") == 0) {
        fs_cat(FS_LOG_FILE);
}
    else {
        kout.print("Unknown: ");
        kout.println(cmd);
        kout.println("Type 'help'");
}
}

void shell_prompt() {
    if (kernel.root_mode) {
        kout.print("Picomimi# ");
} else {
        kout.print("Picomimi~> ");
    }
}

void idle_task(void* arg);
void display_task(void* arg);
void display_init(uint32_t id);
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
void term_render() {
    if(!kernel.display_alive) return;
    
    tft.setTextSize(1);
    char line_buffer[TERM_COLS + 2];
    int lines_to_show = min(history_count, TERM_ROWS);
int last_visible_idx = (history_head - view_offset + SCROLLBACK_ROWS) % SCROLLBACK_ROWS;
for (int r = 0; r < TERM_ROWS; r++) {
        int history_idx = (last_visible_idx - (TERM_ROWS - 1 - r) + SCROLLBACK_ROWS) % SCROLLBACK_ROWS;
if (r < TERM_ROWS - lines_to_show) {
            line_buffer[0] = '\0';
} else {
            bool is_input_line = (view_offset == 0 && history_idx == history_head);
if (is_input_line) {
                snprintf(line_buffer, sizeof(line_buffer), "%s%s_", term_history[history_idx], cmd_buffer);
} else {
                strncpy(line_buffer, term_history[history_idx], sizeof(line_buffer) -1);
line_buffer[sizeof(line_buffer) - 1] = '\0';
            }
        }

        if (strcmp(line_buffer, screen_buffer[r]) != 0) {
            tft.fillRect(0, r * TERM_CHAR_H, LCD_WIDTH, TERM_CHAR_H, ILI9341_BLACK);
if (line_buffer[0] != '\0') {
                tft.setCursor(0, r * TERM_CHAR_H);
bool is_input_line = (view_offset == 0 && history_idx == history_head);
if (is_input_line) {
                    tft.setTextColor(ILI9341_CYAN);
tft.print(term_history[history_head]);
                    tft.setTextColor(ILI9341_YELLOW);
                    tft.print(cmd_buffer);
                    tft.print("_");
                } else {
                    tft.setTextColor(ILI9341_CYAN);
tft.print(line_buffer);
                }
            }
            
            strncpy(screen_buffer[r], line_buffer, sizeof(screen_buffer[0]) -1);
screen_buffer[r][sizeof(screen_buffer[0]) -1] = '\0';
        }
    }
}

void idle_task(void* arg) {
    task_sleep(100);
}

void display_init(uint32_t id) {
    kout.println("[DISPLAY] Terminal initialized");
}

void display_task(void* arg) {
    if (!kernel.display_alive) {
        task_sleep(10000);
return;
    }
    
    if (ui_mode_changed) {
        ui_mode_changed = false;
        
        if (current_ui_mode == UI_TERMINAL) {
            memset(screen_buffer, 1, sizeof(screen_buffer));
term_dirty = true;
        } else if (current_ui_mode == UI_KEYBOARD) {
            keyboard_render_full();
        } else if (current_ui_mode == UI_APPLICATION) {
            // When switching TO an application, just clear the screen.
            // The app's task is responsible for drawing its own UI.
            tft.fillScreen(ILI9341_BLACK);
        }
    }

    // Only render the terminal if we are in terminal mode.
    // If in UI_APPLICATION mode, this task does nothing, yielding
    // screen control to the focused application's task.
    if (current_ui_mode == UI_TERMINAL) {
        if (term_dirty) {
            term_dirty = false;
term_render();
        }
    }

    task_sleep(33);
}

void display_deinit() {
    kout.println("[DISPLAY] DEINIT");
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
            strncat(term_history[history_head], cmd_buffer, TERM_COLS - history_col);
kout.println();

            shell_execute(cmd_buffer);
            cmd_pos = 0;
            memset(cmd_buffer, 0, sizeof(cmd_buffer));
            
            if (!kernel.shell_alive) {
                task_sleep(10000);
return;
            }
            
            shell_prompt();
term_dirty = true;
            
        } else if (c == '\b' || c == 127) {
            if (cmd_pos > 0) {
                cmd_pos--;
cmd_buffer[cmd_pos] = '\0';
                Serial.write('\b'); Serial.write(' '); Serial.write('\b');
                term_dirty = true;
}
        } else if (cmd_pos < sizeof(cmd_buffer) - 1) {
            cmd_buffer[cmd_pos++] = c;
Serial.write(c);
            term_dirty = true;
        }
    }
    
    task_sleep(10);
}

void shell_deinit() {
    kout.println("[SHELL] DEINIT");
    kernel.shell_alive = false;
    cmd_pos = 0;
    memset(cmd_buffer, 0, sizeof(cmd_buffer));
}

void input_task(void* arg) {
    if (!kernel.input_alive) {
        task_sleep(10000);
        return;
    }

    static unsigned long last_onoff_press = 0;
    
    // Check for the UI cycle button (GPIO 9)
    if (gpio_read_fast(BTN_ONOFF) && (millis() - last_onoff_press > 250)) {
        last_onoff_press = millis();
        
        // *** FIX v9.3: Properly handle GUI focus when switching UI modes ***
        if (current_ui_mode == UI_APPLICATION) {
            // When leaving an application, we must release its GUI focus.
            // This stops the app from drawing and tells the display task
            // to take over and render the terminal again.
            if (kernel.gui_focus_task_id != -1) {
                // This function handles resetting the focus ID and switching
                // the UI mode back to UI_TERMINAL.
                k_release_gui_focus(kernel.gui_focus_task_id);
            } else {
                // Fallback in case state is inconsistent
                current_ui_mode = UI_TERMINAL;
                ui_mode_changed = true;
            }
        } else if (current_ui_mode == UI_TERMINAL) {
            current_ui_mode = UI_KEYBOARD;
            ui_mode_changed = true;
        } else if (current_ui_mode == UI_KEYBOARD) {
            if (kernel.gui_focus_task_id != -1) {
                // If an app was running, we can switch back to it
                current_ui_mode = UI_APPLICATION;
            } else {
                current_ui_mode = UI_TERMINAL;
            }
            ui_mode_changed = true;
        }
        
        task_sleep(20);
        return;
    }
    
    // If we are in-app, the app polls for its own input. This task does nothing else.
    if (current_ui_mode == UI_APPLICATION) {
        task_sleep(20);
        return; 
    }

    // If we are on keyboard, handle keyboard input
    if (current_ui_mode == UI_KEYBOARD) {
        keyboard_handle_input();
        task_sleep(20);
        return;
    }

    // --- Terminal Scroll Logic (Only runs if current_ui_mode == UI_TERMINAL) ---
    uint8_t current_state = 0;
    if (gpio_read_fast(BTN_TOP)) current_state |= 0x04;
    if (gpio_read_fast(BTN_BOTTOM)) current_state |= 0x08;
    
    if (current_state & 0x04) {
        if (scroll_hold_state != 0x04) {
            scroll_hold_state = 0x04;
next_scroll_time = millis() + SCROLL_INITIAL_DELAY;
            view_offset++;
            term_dirty = true;
        } else if (millis() >= next_scroll_time) {
            next_scroll_time = millis() + SCROLL_REPEAT_DELAY;
view_offset++;
            term_dirty = true;
        }
    } else if (scroll_hold_state == 0x04) {
        scroll_hold_state = 0;
    }

    if (current_state & 0x08) {
        if (scroll_hold_state != 0x08) {
            scroll_hold_state = 0x08;
next_scroll_time = millis() + SCROLL_INITIAL_DELAY;
            view_offset--;
            term_dirty = true;
        } else if (millis() >= next_scroll_time) {
            next_scroll_time = millis() + SCROLL_REPEAT_DELAY;
view_offset--;
            term_dirty = true;
        }
    } else if (scroll_hold_state == 0x08) {
        scroll_hold_state = 0;
    }

    int max_offset = max(0, history_count - TERM_ROWS);
    if (view_offset > max_offset) view_offset = max_offset;
if (view_offset < 0) view_offset = 0;
    
    task_sleep(20);
}


void input_deinit() {
    kout.println("[INPUT] DEINIT");
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
    kout.println("[CPUMON] DEINIT");
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
    kout.println("[TEMPMON] DEINIT");
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
    kout.println("[VFS] DEINIT");
    vfs_unmount();
    
    if (kernel.vfs_data) {
        kfree(kernel.vfs_data);
kernel.vfs_data = NULL;
    }
    
    if (kernel.vfs_sb) {
        kfree(kernel.vfs_sb);
kernel.vfs_sb = NULL;
    }
    
    kernel.vfs_active = false;
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
    kout.println("[FS] DEINIT - Closing all files");
for (int i = 0; i < FS_MAX_OPEN_FILES; i++) {
        if (kernel.fs_open_files[i].open) {
            kernel.fs_open_files[i].handle.close();
kernel.fs_open_files[i].open = false;
        }
    }
    
    fs_unmount();
    kernel.fs_alive = false;
}

void counter_task(void* arg) {
    static uint32_t counter = 0;
    counter++;
    task_sleep(1000);
}

void counter_deinit() {
    kout.println("[COUNTER] Module unloaded");
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

void watchdog_deinit() {
    kout.println("[WATCHDOG] Module unloaded");
}

ModuleCallbacks counter_callbacks = {
    .init = NULL,
    .tick = counter_task,
    .deinit = counter_deinit
};
ModuleCallbacks watchdog_callbacks = {
    .init = NULL,
    .tick = watchdog_task,
    .deinit = watchdog_deinit
};
void addModules() {
    kout.println("\n=== Loading User Modules ===");
task_create("counter", NULL, NULL, 5,
                TASK_TYPE_MODULE, 0, 0,
                OOM_PRIORITY_NEVER, 1 * 1024, &counter_callbacks,
                "Simple counter module");
kout.println("[OK] Counter module");
    
    task_create("watchdog", NULL, NULL, 3,
                TASK_TYPE_MODULE, TASK_FLAG_PROTECTED, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &watchdog_callbacks,
                "System health watchdog");
kout.println("[OK] Watchdog module");
    
    kout.println("=== Module Loading Complete ===\n");
}

void memhog_spawn();
void cpuburn_spawn();
void stress_spawn();
void snake_spawn();
void calc_spawn();
void clock_spawn();
void sysmon_spawn();
void apple_spawn(); // Forward declare Bad Apple spawner

void memhog_task(void* arg) {
    TCB* me = &kernel.tasks[kernel.current_task];
if (me->state == TASK_TERMINATED) {
        task_sleep(10000);
        return;
}
    
    kout.println("\n[MEMHOG] Starting memory stress test");
    klog(1, "MEMHOG: Started");
    
    void* blocks[50];
uint32_t count = 0;
    
    while (count < 50) {
        if (me->state == TASK_TERMINATED) {
            kout.println("[MEMHOG] Killed");
task_sleep(10000);
            return;
        }
        
        size_t size = 2048 + (random(0, 8192));
void* ptr = kmalloc(size, kernel.current_task);
        
        if (ptr) {
            blocks[count++] = ptr;
memset(ptr, 0xAA, size);
            if (count % 5 == 0) {
                kout.print("[MEMHOG] Block ");
kout.print(count);
                kout.print(": ");
                kout.print(me->mem_used / 1024);
                kout.println(" KB");
            }
            task_sleep(200);
} else {
            kout.println("[MEMHOG] Allocation failed!");
            break;
}
    }
    
    if (me->state != TASK_TERMINATED) {
        kout.println("[MEMHOG] Cleaning up");
for (uint32_t i = 0; i < count; i++) {
            kfree(blocks[i]);
}
        kout.print("[MEMHOG] Peak: ");
        kout.print(me->mem_peak / 1024);
        kout.println(" KB");
        klog(0, "MEMHOG: Completed");
me->state = TASK_TERMINATED;
        me->last_run = get_time_ms();
    }
    
    task_sleep(10000);
}

void memhog_deinit() {
    kout.println("[MEMHOG] Cleanup complete");
}

ModuleCallbacks memhog_callbacks = {
    .init = NULL,
    .tick = memhog_task,
    .deinit = memhog_deinit
};
void memhog_spawn() {
    uint32_t tid = task_create("memhog", NULL, NULL, 5,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 30000,
                               OOM_PRIORITY_LOW, 50 * 1024, &memhog_callbacks,
                    
           "Memory stress test (OOM=LOW)");
if (tid > 0) {
        kout.println("Memory stress test spawned");
}
}

void cpuburn_task(void* arg) {
    TCB* me = &kernel.tasks[kernel.current_task];
if (me->state == TASK_TERMINATED) {
        task_sleep(10000);
        return;
}
    
    kout.println("\n[CPUBURN] Starting CPU stress");
    klog(1, "CPUBURN: Started");
for (int iter = 0; iter < 100; iter++) {
        if (me->state == TASK_TERMINATED) {
            kout.println("[CPUBURN] Killed");
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
            kout.print("[CPUBURN] ");
kout.print(iter);
            kout.println("%");
        }
        
        task_sleep(5);
}
    
    if (me->state != TASK_TERMINATED) {
        kout.println("[CPUBURN] Complete");
klog(0, "CPUBURN: Completed");
        me->state = TASK_TERMINATED;
        me->last_run = get_time_ms();
    }
    
    task_sleep(10000);
}

void cpuburn_deinit() {
    kout.println("[CPUBURN] Cleanup complete");
}

ModuleCallbacks cpuburn_callbacks = {
    .init = NULL,
    .tick = cpuburn_task,
    .deinit = cpuburn_deinit
};
void cpuburn_spawn() {
    uint32_t tid = task_create("cpuburn", NULL, NULL, 5,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 30000,
                               OOM_PRIORITY_NORMAL, 4 * 1024, &cpuburn_callbacks,
                    
           "CPU stress test (OOM=NORM)");
if (tid > 0) {
        kout.println("CPU stress test spawned");
}
}

void stress_task(void* arg) {
    TCB* me = &kernel.tasks[kernel.current_task];
    
    kout.println("\n[STRESS] Full system stress test");
    klog(2, "STRESS: Started");
void* blocks[20];
    uint32_t block_count = 0;
    
    for (uint32_t cycle = 0; cycle < 20; cycle++) {
        if (me->state == TASK_TERMINATED) {
            kout.println("[STRESS] Terminated");
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
        
        kout.print("[STRESS] Cycle ");
        kout.print(cycle);
kout.println("/20");
        
        task_sleep(100);
        
        if (block_count > 10 && random(0, 100) < 30) {
            uint32_t idx = random(0, block_count);
kfree(blocks[idx]);
            blocks[idx] = blocks[--block_count];
        }
    }
    
    kout.println("[STRESS] Cleanup");
for (uint32_t i = 0; i < block_count; i++) {
        kfree(blocks[i]);
}
    
    kout.println("[STRESS] Complete!");
    klog(0, "STRESS: Completed");
    
    me->state = TASK_TERMINATED;
    me->last_run = get_time_ms();
    task_sleep(10000);
}

void stress_deinit() {
    kout.println("[STRESS] Cleanup complete");
}

ModuleCallbacks stress_callbacks = {
    .init = NULL,
    .tick = stress_task,
    .deinit = stress_deinit
};
void stress_spawn() {
    uint32_t tid = task_create("stress", NULL, NULL, 4,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 60000,
                               OOM_PRIORITY_LOW, 100 * 1024, &stress_callbacks,
                    
           "System stress test (OOM=LOW)");
if (tid > 0) {
        kout.println("System stress test spawned");
}
}

// --- SNAKE GAME (UISocket Example) ---
struct SnakeGame {
    UISocket ui; // The API to interact with the GUI system
    int16_t snake_x[50];
    int16_t snake_y[50];
    uint8_t snake_len;
    int8_t dir_x, dir_y;
    int16_t food_x, food_y;
    uint32_t score;
    bool game_over;
    bool has_focus; // Track if we are the active GUI
    uint32_t last_input_time;
};

SnakeGame* snake_game = NULL;

void snake_init(uint32_t id) {
    snake_game = (SnakeGame*)kmalloc(sizeof(SnakeGame), id);
    if (!snake_game) {
        kout.println("[SNAKE] Alloc failed!");
        return; // The tick function will see the null pointer and terminate.
    }
    memset(snake_game, 0, sizeof(SnakeGame));

    // Register with the GUI system to get the API
    k_register_gui_app(&snake_game->ui);

    // Set up initial game state, but do not request focus or draw yet.
    snake_game->snake_len = 3;
    snake_game->snake_x[0] = 160;
    snake_game->snake_y[0] = 120;
    snake_game->dir_x = 10;
    snake_game->dir_y = 0;
    snake_game->food_x = (random(1, 31)) * 10;
    snake_game->food_y = (random(1, 23)) * 10;
    
    klog(0, "SNAKE: Initialized");
}

void snake_task(void* arg) {
    if (!snake_game) {
        // Init must have failed. Terminate.
        brutal_task_kill(kernel.current_task);
        task_sleep(10000); // Give scheduler time to kill us
        return;
    }

    // --- Focus Management ---
    // On first run, or if we lost and are regaining focus, we must acquire it.
    if (!snake_game->has_focus) {
        if (!snake_game->ui.request_focus(kernel.current_task)) {
             kout.println("[SNAKE] Could not get focus, terminating.");
             brutal_task_kill(kernel.current_task);
             task_sleep(10000);
             return;
        }
        snake_game->has_focus = true;
        snake_game->ui.clear_screen(); // Clear screen now that we have focus
    }

    // Double-check we still have focus before doing anything.
    // This handles the case where the user cycles away from the app.
    if (kernel.gui_focus_task_id != kernel.current_task) {
        snake_game->has_focus = false; // We lost focus, mark it.
        task_sleep(200); // Wait until we (maybe) get focus back
        return;
    }
    
    // --- Game Over State ---
    if (snake_game->game_over) {
        snake_game->ui.draw_text(160, 100, "Game Over", ILI9341_RED, 3, true);
        char score_buf[20];
        snprintf(score_buf, sizeof(score_buf), "Score: %d", snake_game->score);
        snake_game->ui.draw_text(160, 140, score_buf, ILI9341_WHITE, 2, true);
        snake_game->ui.draw_text(160, 180, "Press any key to exit", ILI9341_YELLOW, 1, true);

        if (snake_game->ui.get_button_state(UI_BTN_TOP) ||
            snake_game->ui.get_button_state(UI_BTN_BOTTOM) ||
            snake_game->ui.get_button_state(UI_BTN_LEFT) ||
            snake_game->ui.get_button_state(UI_BTN_RIGHT)) {
            
            // Use the kernel's kill function to properly terminate and clean up.
            // This will call snake_deinit, release GUI focus, and free memory.
            brutal_task_kill(kernel.current_task);

            // The task is now terminated. Return immediately so the scheduler
            // can process the TERMINATED state. Do not call any function
            // that might change the task state again (like task_sleep).
            return;
        }
        task_sleep(50);
        return;
    }

    // --- Input Handling ---
    uint32_t now = get_time_ms();
    if (now - snake_game->last_input_time > 80) {
        bool moved = false;
        if (snake_game->ui.get_button_state(UI_BTN_TOP) && snake_game->dir_y == 0) {
            snake_game->dir_x = 0; snake_game->dir_y = -10; moved = true;
        } else if (snake_game->ui.get_button_state(UI_BTN_BOTTOM) && snake_game->dir_y == 0) {
            snake_game->dir_x = 0; snake_game->dir_y = 10; moved = true;
        } else if (snake_game->ui.get_button_state(UI_BTN_LEFT) && snake_game->dir_x == 0) {
            snake_game->dir_x = -10; snake_game->dir_y = 0; moved = true;
        } else if (snake_game->ui.get_button_state(UI_BTN_RIGHT) && snake_game->dir_x == 0) {
            snake_game->dir_x = 10; snake_game->dir_y = 0; moved = true;
        }
        if (moved) {
            snake_game->last_input_time = now;
        }
    }


    // --- Game Logic ---
    snake_game->ui.fill_rect(snake_game->snake_x[snake_game->snake_len - 1],
                             snake_game->snake_y[snake_game->snake_len - 1],
                             10, 10, ILI9341_BLACK);

    for (int i = snake_game->snake_len - 1; i > 0; i--) {
        snake_game->snake_x[i] = snake_game->snake_x[i-1];
        snake_game->snake_y[i] = snake_game->snake_y[i-1];
    }
    snake_game->snake_x[0] += snake_game->dir_x;
    snake_game->snake_y[0] += snake_game->dir_y;

    if (snake_game->snake_x[0] == snake_game->food_x && snake_game->snake_y[0] == snake_game->food_y) {
        snake_game->score++;
        if (snake_game->snake_len < 49) snake_game->snake_len++;
        snake_game->food_x = (random(1, 31)) * 10;
        snake_game->food_y = (random(1, 23)) * 10;
    }

    if (snake_game->snake_x[0] < 0 || snake_game->snake_x[0] > 310 ||
        snake_game->snake_y[0] < 0 || snake_game->snake_y[0] > 230) {
        snake_game->game_over = true;
    }
    for (int i = 1; i < snake_game->snake_len; i++) {
        if (snake_game->snake_x[0] == snake_game->snake_x[i] && snake_game->snake_y[0] == snake_game->snake_y[i]) {
            snake_game->game_over = true;
        }
    }


    // --- Drawing ---
    snake_game->ui.fill_rect(snake_game->food_x, snake_game->food_y, 10, 10, ILI9341_RED);
    for (int i = 0; i < snake_game->snake_len; i++) {
        snake_game->ui.fill_rect(snake_game->snake_x[i], snake_game->snake_y[i], 10, 10,
                                 i == 0 ? ILI9341_GREEN : ILI9341_YELLOW);
    }
    char score_buf[20];
    snprintf(score_buf, sizeof(score_buf), "Score: %d", snake_game->score);
    snake_game->ui.fill_rect(0, 0, 80, 10, ILI9341_BLACK);
    snake_game->ui.draw_text(2, 2, score_buf, ILI9341_WHITE, 1, false);

    task_sleep(150);
}

void snake_deinit() {
    if (snake_game) {
        // We can use kernel.current_task because deinit is called in our context
        if (kernel.gui_focus_task_id == kernel.current_task) {
            snake_game->ui.release_focus(kernel.current_task);
        }
        kfree(snake_game);
        snake_game = NULL;
    }
    klog(0, "SNAKE: Deinitialized");
}

ModuleCallbacks snake_callbacks = {
    .init = snake_init,
    .tick = snake_task,
    .deinit = snake_deinit
};
void snake_spawn() {
    uint32_t tid = task_create("snake", NULL, NULL, 6,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_NORMAL, 4 * 1024, &snake_callbacks,
                               "Snake game (OOM=NORM)");
if (tid > 0) {
        kout.println("Snake game started");
}
}


// --- BAD APPLE!! GIF PLAYER ---
#define GIF_FILENAME "/Bad_Apple.gif"

struct BadAppleApp {
    UISocket ui;
    AnimatedGIF gif;
    File gifFile;
    bool has_focus;
};
BadAppleApp* bad_apple_app = NULL;

// --- GIF Callback Functions (adapted for SD.h)---

void* GIFOpenFile(const char* fname, int32_t* pSize) {
    if (!bad_apple_app) return NULL;
    bad_apple_app->gifFile = SD.open(fname, FILE_READ);
    if (bad_apple_app->gifFile) {
        *pSize = bad_apple_app->gifFile.size();
        return (void*)&(bad_apple_app->gifFile);
    }
    return NULL;
}

void GIFCloseFile(void* pHandle) {
    if (!bad_apple_app) return;
    File* f = static_cast<File*>(pHandle);
    if (f) f->close();
}

int32_t GIFReadFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    int32_t iBytesRead = iLen;
    File* f = static_cast<File*>(pFile->fHandle);
    if (!f) return 0;

    int32_t iRemaining = pFile->iSize - pFile->iPos;
    if (iRemaining < iLen) {
        iBytesRead = iRemaining;
    }
    if (iBytesRead <= 0) {
        return 0;
    }

    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
}

int32_t GIFSeekFile(GIFFILE* pFile, int32_t iPosition) {
    File* f = static_cast<File*>(pFile->fHandle);
    if (!f) return 0;
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
    return pFile->iPos;
}

void GIFDraw(GIFDRAW* pDraw) {
    uint8_t* s;
    uint16_t* d, * usPalette, usTemp[320];
    int x, y, iWidth = pDraw->iWidth;

    if (iWidth > 320) iWidth = 320;

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y;

    if (y >= 240 || pDraw->iX >= 320 || iWidth < 1) return;

    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) {
        for (x = 0; x < iWidth; x++) {
            if (s[x] == pDraw->ucTransparent) {
                s[x] = pDraw->ucBackground;
            }
        }
        pDraw->ucHasTransparency = 0;
    }

    if (pDraw->ucHasTransparency) {
        uint8_t* pEnd, c, ucTransparent = pDraw->ucTransparent;
        int iCount;
        pEnd = s + iWidth;
        x = 0;
        iCount = 0;
        while (x < iWidth) {
            c = ucTransparent - 1;
            d = usTemp;
            while (c != ucTransparent && s < pEnd) {
                c = *s++;
                if (c == ucTransparent) s--;
                else *d++ = usPalette[c];
            }
            iCount = (int)(d - usTemp);
            if (iCount) {
                tft.startWrite();
                tft.setAddrWindow(pDraw->iX + x, y, iCount, 1);
                tft.writePixels(usTemp, iCount, false, false);
                tft.endWrite();
                x += iCount;
            }
            c = ucTransparent;
            iCount = 0;
            while (c == ucTransparent && s < pEnd) {
                c = *s++;
                if (c == ucTransparent) iCount++;
                else s--;
            }
            if (iCount) {
                x += iCount;
            }
        }
    } else {
        s = pDraw->pPixels;
        for (x = 0; x < iWidth; x++) {
            usTemp[x] = usPalette[*s++];
        }
        tft.startWrite();
        tft.setAddrWindow(pDraw->iX, y, iWidth, 1);
        tft.writePixels(usTemp, iWidth, false, false);
        tft.endWrite();
    }
}

void apple_init(uint32_t id) {
    bad_apple_app = (BadAppleApp*)kmalloc(sizeof(BadAppleApp), id);
    if (!bad_apple_app) {
        kout.println("[APPLE] Alloc failed!");
        return;
    }
    memset(bad_apple_app, 0, sizeof(BadAppleApp));
    k_register_gui_app(&bad_apple_app->ui);
    bad_apple_app->gif.begin(LITTLE_ENDIAN_PIXELS);
    klog(0, "APPLE: Initialized");
}

void apple_task(void* arg) {
    if (!bad_apple_app) {
        brutal_task_kill(kernel.current_task);
        return;
    }

    if (!bad_apple_app->has_focus) {
        if (!bad_apple_app->ui.request_focus(kernel.current_task)) {
            kout.println("[APPLE] Could not get focus, terminating.");
            brutal_task_kill(kernel.current_task);
            return;
        }
        bad_apple_app->has_focus = true;
    }
    
    if (kernel.gui_focus_task_id != kernel.current_task) {
        bad_apple_app->has_focus = false;
        task_sleep(200);
        return;
    }
    
    if (!SD.exists(GIF_FILENAME)) {
        kout.print("\n[APPLE] ERROR: '");
        kout.print(GIF_FILENAME);
        kout.println("' not found on SD card!");
        brutal_task_kill(kernel.current_task);
        return;
    }

    if (bad_apple_app->gif.open(GIF_FILENAME, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
        int x = (LCD_WIDTH - bad_apple_app->gif.getCanvasWidth()) / 2;
        int y = (LCD_HEIGHT - bad_apple_app->gif.getCanvasHeight()) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        
        kout.println("[APPLE] Playing Bad Apple!!");
        
        while (bad_apple_app->gif.playFrame(true, NULL)) {
            // Check for exit condition
            if (k_ui_get_button_state(UI_BTN_TOP) || k_ui_get_button_state(UI_BTN_BOTTOM) ||
                k_ui_get_button_state(UI_BTN_LEFT) || k_ui_get_button_state(UI_BTN_RIGHT)) {
                kout.println("[APPLE] Playback stopped by user.");
                break; // Exit the loop
            }
        }
        bad_apple_app->gif.close();
    } else {
        kout.println("\n[APPLE] Error opening GIF file!");
    }

    brutal_task_kill(kernel.current_task);
}

void apple_deinit() {
    if (bad_apple_app) {
        if (kernel.gui_focus_task_id == kernel.current_task) {
            bad_apple_app->ui.release_focus(kernel.current_task);
        }
        kfree(bad_apple_app);
        bad_apple_app = NULL;
    }
    kout.println("[APPLE] Player closed.");
    klog(0, "APPLE: Deinitialized");
}

ModuleCallbacks apple_callbacks = {
    .init = apple_init,
    .tick = apple_task,
    .deinit = apple_deinit
};

void apple_spawn() {
    uint32_t tid = task_create("apple", NULL, NULL, 6,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_HIGH, 30 * 1024, &apple_callbacks,
                               "Bad Apple!! GIF Player (OOM=HIGH)");
    if (tid > 0) {
        kout.println("Bad Apple!! player starting...");
    }
}


void calc_task(void* arg) {
    task_sleep(100);
}

void calc_init(uint32_t id) {
    kout.println("\n=== CALCULATOR ===");
    kout.println("Simple calculator app");
klog(0, "CALC: Started");
}

void calc_deinit() {
    kout.println("[CALC] Exiting");
    klog(0, "CALC: Exited");
}

ModuleCallbacks calc_callbacks = {
    .init = calc_init,
    .tick = calc_task,
    .deinit = calc_deinit
};
void calc_spawn() {
    uint32_t tid = task_create("calc", NULL, NULL, 7,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_NORMAL, 4 * 1024, &calc_callbacks,
                    
           "Calculator (OOM=NORM)");
if (tid > 0) {
        kout.println("Calculator started");
}
}

struct ClockData {
    uint32_t start_time;
};

ClockData* clock_data = NULL;
void clock_init(uint32_t id) {
    clock_data = (ClockData*)kmalloc(sizeof(ClockData), id);
    if (!clock_data) return;
    
    clock_data->start_time = get_time_ms();
    
    kout.println("[CLOCK] Started");
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
    
    task_sleep(1000);
}

void clock_deinit() {
    kout.println("[CLOCK] Stopped");
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
    uint32_t tid = task_create("clock", NULL, NULL, 6,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_NORMAL, 2 * 1024, &clock_callbacks,
                    
           "Digital clock (OOM=NORM)");
if (tid > 0) {
        kout.println("Digital clock started");
}
}

void sysmon_init(uint32_t id) {
    kout.println("[SYSMON] Started");
    klog(0, "SYSMON: Started");
}

void sysmon_task(void* arg) {
    task_sleep(1000);
}

void sysmon_deinit() {
    kout.println("[SYSMON] Stopped");
    klog(0, "SYSMON: Stopped");
}

ModuleCallbacks sysmon_callbacks = {
    .init = sysmon_init,
    .tick = sysmon_task,
    .deinit = sysmon_deinit
};
void sysmon_spawn() {
    uint32_t tid = task_create("sysmon", NULL, NULL, 5,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_HIGH, 2 * 1024, &sysmon_callbacks,
                    
           "System monitor (OOM=HIGH)");
if (tid > 0) {
        kout.println("System monitor started");
}
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    SPI.setRX(SD_MISO);
    SPI.setTX(SD_MOSI);
    SPI.setSCK(SD_SCK);

    randomSeed(micros());

    pinMode(LCD_LED, OUTPUT);
    digitalWrite(LCD_LED, HIGH);
    tft.begin();
    tft.setRotation(3);
    kernel.display_alive = true;
memset(screen_buffer, 1, sizeof(screen_buffer));
    
    kout.println("========================================");
    kout.println("  RP2040 Kernel v9.6.1");
    kout.println("  Bad Apple!! Edition (Compiler Fix)");
    kout.println("========================================");
    kout.println("Initializing...");
uint8_t buttons[] = {BTN_LEFT, BTN_RIGHT, BTN_TOP, BTN_BOTTOM, 
                         BTN_SELECT, BTN_START, BTN_A, BTN_B, BTN_ONOFF};
for (int i = 0; i < 9; i++) {
        pinMode(buttons[i], INPUT_PULLUP);
}
    kout.println("[OK] Input system");
    
    temp_init();
    kernel.temperature = read_temperature();
kout.print("[OK] Temperature (");
    kout.print(kernel.temperature, 1);
    kout.println("C)");
    
    mem_init();
    kout.println("[OK] Memory manager");
    
    task_init();
kout.println("[OK] Task scheduler (Priority-based)");
    vfs_init();
    
    fs_init();
    if (kernel.fs_available) {
        fs_mount();
}
    
    kout.println("\n=== Loading System Tasks ===");
task_create("idle", idle_task, NULL, 0,
                TASK_TYPE_KERNEL, TASK_FLAG_PROTECTED | TASK_FLAG_CRITICAL | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 0, NULL,
                "Kernel idle task");
kout.println("[OK] Idle (KERNEL - Pri 0)");
    
    task_create("display", NULL, NULL, 8,
                TASK_TYPE_DRIVER, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 4 * 1024, &display_callbacks,
                "ILI9341 display driver");
kout.println("[OK] Display driver (Pri 8)");
    
    task_create("input", NULL, NULL, 9,
                TASK_TYPE_DRIVER, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &input_callbacks,
                "Button input driver");
kout.println("[OK] Input driver (Pri 9)");
    
    task_create("shell", NULL, NULL, 7,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 4 * 1024, &shell_callbacks,
                "Command shell service");
kout.println("[OK] Shell service (Pri 7)");
    
    task_create("cpumon", NULL, NULL, 1,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &cpumon_callbacks,
                "CPU usage monitor");
kout.println("[OK] CPU monitor (Pri 1)");
    
    task_create("tempmon", NULL, NULL, 1,
                TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                OOM_PRIORITY_NEVER, 2 * 1024, &tempmon_callbacks,
                "Temperature monitor");
kout.println("[OK] Temp monitor (Pri 1)");
    
    if (kernel.fs_available) {
        task_create("fs", NULL, NULL, 2,
                    TASK_TYPE_SERVICE, TASK_FLAG_PROTECTED | TASK_FLAG_RESPAWN, 0,
                    OOM_PRIORITY_NEVER, 4 * 1024, &fs_callbacks,
                    "SD filesystem service");
kout.println("[OK] FS service (Pri 2)");
    }
    
    addModules();
    
    kout.println("========================================");
    kout.println("Kernel boot complete!");
    kout.println("========================================");
kout.print("Heap:      "); kout.print(HEAP_SIZE / 1024); kout.println(" KB");
    kout.print("Tasks:     "); kout.print(kernel.task_count);
kout.println(" loaded");
    kout.print("VFS:       Inactive (use 'vfscreate')");
    kout.println();
if (kernel.fs_available) {
        kout.print("SD Card:   "); 
        kout.print((uint32_t)(kernel.fs_total_bytes / (1024 * 1024)));
kout.println(" MB");
    } else {
        kout.println("SD Card:   Unavailable");
}
    
    kout.println("\n=== v9.6.1 Bad Apple!! Edition ===");
    kout.println("- Type 'apple' to play the GIF.");
    kout.println("- Make sure 'Bad_Apple.gif' is on the SD card root.");
    
    kout.println("\nType 'help' for commands");
klog(0, "KERNEL: Boot complete v9.6.1");
    
    shell_prompt();
    term_dirty = true;
    kernel.running = true;
}

void loop() {
    if (kernel.current_task >= MAX_TASKS) {
        disable_all_interrupts();
while(1) { __asm__ volatile ("wfi"); }
    }
    
    if (!kernel.running) {
        disable_all_interrupts();
while(1) { __asm__ volatile ("wfi"); }
    }
    
    if (kernel.task_count == 0) {
        disable_all_interrupts();
kout.println("NO TASKS - HALT");
        Serial.flush();
        while(1) { __asm__ volatile ("wfi");
}
    }
    
    uint64_t loop_start = get_time_us();
    
    scheduler_tick();
if (kernel.tasks[0].state == TASK_TERMINATED || kernel.tasks[0].entry == NULL) {
        disable_all_interrupts();
        digitalWrite(LCD_LED, LOW);
kout.println("IDLE TASK DEAD - HALT");
        Serial.flush();
        while(1) { __asm__ volatile ("wfi");
}
    }
    
    TCB* task = &kernel.tasks[kernel.current_task];
if (task->state == TASK_TERMINATED) {
        task_yield();
        return;
}
    
    if ((task->state == TASK_READY || task->state == TASK_RUNNING) && (task->entry || (task->callbacks && task->callbacks->tick))) {
        task->state = TASK_RUNNING;
uint64_t task_start = get_time_us();
        
        // Use callbacks if available, otherwise use direct entry point
        if (task->callbacks && task->callbacks->tick) {
            task->callbacks->tick(task->arg);
        } else if (task->entry) {
            task->entry(task->arg);
        }

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

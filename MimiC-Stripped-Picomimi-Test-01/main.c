/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC - Main Entry Point                                                 ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Self-Hosted C Compiler & Runtime for RP2040/RP2350                       ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"

#include <stdio.h>
#include <string.h>

#include "mimic.h"
#include "mimic_fat32.h"
#include "mimic_cc.h"

// ============================================================================
// VERSION
// ============================================================================

#define MIMIC_VERSION_STRING "1.0.0-alpha"

// ============================================================================
// SHELL COMMANDS
// ============================================================================

typedef struct {
    const char* name;
    const char* help;
    void (*handler)(int argc, char** argv);
} ShellCommand;

static void cmd_help(int argc, char** argv);
static void cmd_ls(int argc, char** argv);
static void cmd_cat(int argc, char** argv);
static void cmd_cc(int argc, char** argv);
static void cmd_run(int argc, char** argv);
static void cmd_mem(int argc, char** argv);
static void cmd_ps(int argc, char** argv);
static void cmd_kill(int argc, char** argv);
static void cmd_reboot(int argc, char** argv);

static const ShellCommand commands[] = {
    { "help",   "Show available commands",          cmd_help },
    { "ls",     "List directory contents",          cmd_ls },
    { "cat",    "Display file contents",            cmd_cat },
    { "cc",     "Compile C source file",            cmd_cc },
    { "run",    "Run compiled .mimi binary",        cmd_run },
    { "mem",    "Show memory statistics",           cmd_mem },
    { "ps",     "List running tasks",               cmd_ps },
    { "kill",   "Kill a running task",              cmd_kill },
    { "reboot", "Reboot the system",                cmd_reboot },
    { NULL, NULL, NULL }
};

// ============================================================================
// SHELL IMPLEMENTATION
// ============================================================================

static void cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("\nMimiC Commands:\n");
    printf("═══════════════════════════════════════════════════\n");
    
    for (int i = 0; commands[i].name; i++) {
        printf("  %-10s - %s\n", commands[i].name, commands[i].help);
    }
    
    printf("\nCompiler usage:\n");
    printf("  cc <source.c>           Compile to source.mimi\n");
    printf("  cc <source.c> -o <out>  Compile to specified output\n");
    printf("  run <program.mimi>      Execute compiled program\n");
    printf("\n");
}

static void cmd_ls(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "/";
    
    if (!mimic_fat32_mounted()) {
        printf("Error: Filesystem not mounted\n");
        return;
    }
    
    printf("\nDirectory: %s\n", path);
    printf("───────────────────────────────────────\n");
    
    int dir = mimic_opendir(path);
    if (dir < 0) {
        printf("Error: Cannot open directory\n");
        return;
    }
    
    MimicDirEntry entry;
    while (mimic_readdir(dir, &entry) == MIMIC_OK) {
        if (entry.is_dir) {
            printf("  [DIR]  %s/\n", entry.name);
        } else {
            printf("  %6lu %s\n", entry.size, entry.name);
        }
    }
    
    mimic_closedir(dir);
    printf("\n");
}

static void cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cat <filename>\n");
        return;
    }
    
    int fd = mimic_fopen(argv[1], MIMIC_FILE_READ);
    if (fd < 0) {
        printf("Error: Cannot open file '%s'\n", argv[1]);
        return;
    }
    
    char buf[128];
    int n;
    while ((n = mimic_fread(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    printf("\n");
    
    mimic_fclose(fd);
}

static void cmd_cc(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cc <source.c> [-o output.mimi]\n");
        return;
    }
    
    const char* source = argv[1];
    char output[128];
    
    // Default output name
    strncpy(output, source, sizeof(output) - 6);
    char* dot = strrchr(output, '.');
    if (dot) {
        strcpy(dot, ".mimi");
    } else {
        strcat(output, ".mimi");
    }
    
    // Check for -o option
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            strncpy(output, argv[i + 1], sizeof(output) - 1);
            break;
        }
    }
    
    printf("\nCompiling: %s → %s\n", source, output);
    printf("═══════════════════════════════════════════════════\n");
    
    // Initialize compiler
    CompilerState cc;
    if (mimic_cc_init(&cc) != MIMIC_OK) {
        printf("Error: Failed to initialize compiler\n");
        return;
    }
    
    // Compile
    uint64_t start = time_us_64();
    int result = mimic_cc_compile(&cc, source, output);
    uint64_t elapsed = time_us_64() - start;
    
    if (result == MIMIC_OK) {
        printf("\n✓ Compilation successful\n");
        printf("  Time: %llu ms\n", elapsed / 1000);
        printf("  Tokens: %lu\n", cc.tokens_processed);
        printf("  Code: %lu bytes\n", cc.code_bytes);
    } else {
        printf("\n✗ Compilation failed\n");
        mimic_cc_print_errors(&cc);
    }
    
    mimic_cc_cleanup(&cc);
}

static void cmd_run(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: run <program.mimi>\n");
        return;
    }
    
    printf("\nLoading: %s\n", argv[1]);
    
    int task_id = mimic_task_load(argv[1], 8);  // Default priority 8
    
    if (task_id < 0) {
        printf("Error: Failed to load program (code %d)\n", task_id);
        return;
    }
    
    printf("Started task %d\n\n", task_id);
}

static void cmd_mem(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("\n");
    mimic_dump_memory();
    
    if (mimic_fat32_mounted()) {
        MimicFSInfo fs;
        if (mimic_fs_info(&fs) == MIMIC_OK) {
            printf("\nSD Card:\n");
            printf("  Total:  %llu MB\n", fs.total_bytes / (1024 * 1024));
            printf("  Used:   %llu MB\n", fs.used_bytes / (1024 * 1024));
            printf("  Free:   %llu MB\n", fs.free_bytes / (1024 * 1024));
        }
    }
    printf("\n");
}

static void cmd_ps(int argc, char** argv) {
    (void)argc; (void)argv;
    mimic_dump_tasks();
}

static void cmd_kill(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: kill <task_id>\n");
        return;
    }
    
    int task_id = atoi(argv[1]);
    if (task_id <= 0) {
        printf("Error: Cannot kill kernel task\n");
        return;
    }
    
    mimic_task_kill(task_id);
    printf("Killed task %d\n", task_id);
}

static void cmd_reboot(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Rebooting...\n");
    sleep_ms(100);
    watchdog_reboot(0, 0, 0);
}

// ============================================================================
// SHELL PARSER
// ============================================================================

#define MAX_ARGS 16
#define LINE_BUF_SIZE 256

static void shell_process_line(char* line) {
    // Parse arguments
    char* argv[MAX_ARGS];
    int argc = 0;
    
    char* p = line;
    while (*p && argc < MAX_ARGS) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        
        argv[argc++] = p;
        
        // Find end of argument
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    
    if (argc == 0) return;
    
    // Find and execute command
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].handler(argc, argv);
            return;
        }
    }
    
    printf("Unknown command: %s\n", argv[0]);
    printf("Type 'help' for available commands\n");
}

static void shell_run(void) {
    char line[LINE_BUF_SIZE];
    int pos = 0;
    
    printf("mimic> ");
    fflush(stdout);
    
    while (true) {
        int c = getchar();
        if (c == EOF) {
            sleep_ms(10);
            continue;
        }
        
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[pos] = '\0';
            
            if (pos > 0) {
                shell_process_line(line);
            }
            
            pos = 0;
            printf("mimic> ");
            fflush(stdout);
            
        } else if (c == '\b' || c == 127) {
            // Backspace
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
            
        } else if (c >= 32 && pos < LINE_BUF_SIZE - 1) {
            line[pos++] = c;
            putchar(c);
            fflush(stdout);
        }
    }
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    // Initialize stdio
    stdio_init_all();
    
    // Wait for USB connection
    sleep_ms(2000);
    
    // Print banner
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                               ║\n");
    printf("║   ███╗   ███╗██╗███╗   ███╗██╗ ██████╗                       ║\n");
    printf("║   ████╗ ████║██║████╗ ████║██║██╔════╝                       ║\n");
    printf("║   ██╔████╔██║██║██╔████╔██║██║██║                            ║\n");
    printf("║   ██║╚██╔╝██║██║██║╚██╔╝██║██║██║                            ║\n");
    printf("║   ██║ ╚═╝ ██║██║██║ ╚═╝ ██║██║╚██████╗                       ║\n");
    printf("║   ╚═╝     ╚═╝╚═╝╚═╝     ╚═╝╚═╝ ╚═════╝                       ║\n");
    printf("║                                                               ║\n");
    printf("║   Self-Hosted C Compiler & Runtime v" MIMIC_VERSION_STRING "              ║\n");
    printf("║   Target: " MIMIC_CHIP_NAME "                                             ║\n");
    printf("║                                                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Initialize kernel
    mimic_kernel_init();
    
    // Mount filesystem
    printf("[INIT] Mounting SD card...\n");
    int err = mimic_fat32_mount();
    if (err == MIMIC_OK) {
        printf("[INIT] SD card mounted successfully\n");
        
        // Create MimiC directories if they don't exist
        if (!mimic_exists("/mimic")) {
            printf("[INIT] Creating /mimic directory structure\n");
            mimic_mkdir("/mimic");
            mimic_mkdir("/mimic/src");
            mimic_mkdir("/mimic/bin");
            mimic_mkdir("/mimic/tmp");
            mimic_mkdir("/mimic/sdk");
        }
    } else {
        printf("[INIT] SD card not found (code %d)\n", err);
        printf("[INIT] Compilation features disabled\n");
    }
    
    printf("\n");
    printf("Type 'help' for available commands\n\n");
    
    // Run shell
    shell_run();
    
    return 0;
}

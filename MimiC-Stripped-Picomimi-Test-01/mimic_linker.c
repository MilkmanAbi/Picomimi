/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC Linker - Object File Linking to .mimi Binary                       ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Pass 5: source.o + libs → source.mimi                                    ║
 * ║  Creates position-independent relocatable binaries                        ║
 * ║  RAM usage: ~16-32KB (symbol table + relocation processing)               ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include <string.h>
#include <stdio.h>

#include "mimic.h"
#include "mimic_fat32.h"
#include "mimic_cc.h"

// ============================================================================
// LINKER STATE
// ============================================================================

typedef struct {
    // Combined sections
    uint8_t*        text;
    uint32_t        text_size;
    uint32_t        text_cap;
    
    uint8_t*        rodata;
    uint32_t        rodata_size;
    uint32_t        rodata_cap;
    
    uint8_t*        data;
    uint32_t        data_size;
    uint32_t        data_cap;
    
    uint32_t        bss_size;
    
    // Combined relocations
    MimiReloc*      relocs;
    uint32_t        reloc_count;
    uint32_t        reloc_cap;
    
    // Combined symbols
    MimiSymbol*     symbols;
    uint32_t        sym_count;
    uint32_t        sym_cap;
    
    // Entry point
    uint32_t        entry_offset;
    bool            has_entry;
    
    // Output name
    char            name[16];
    
    // Error handling
    char            error_msg[128];
    uint32_t        error_count;
    
    // Compiler state
    CompilerState*  cc;
} LinkerState;

// ============================================================================
// SYMBOL TABLE MANAGEMENT
// ============================================================================

static int linker_add_symbol(LinkerState* lnk, const MimiSymbol* sym) {
    // Check for duplicate
    for (uint32_t i = 0; i < lnk->sym_count; i++) {
        if (strcmp(lnk->symbols[i].name, sym->name) == 0) {
            // Already exists
            if (sym->type == MIMI_SYM_GLOBAL && lnk->symbols[i].type == MIMI_SYM_EXTERN) {
                // Definition resolves extern
                lnk->symbols[i] = *sym;
                return i;
            } else if (sym->type == MIMI_SYM_GLOBAL && lnk->symbols[i].type == MIMI_SYM_GLOBAL) {
                // Multiple definitions
                snprintf(lnk->error_msg, sizeof(lnk->error_msg),
                         "Multiple definition of '%s'", sym->name);
                lnk->error_count++;
                return -1;
            }
            // Extern seeing another extern or definition - OK
            return i;
        }
    }
    
    // Add new symbol
    if (lnk->sym_count >= lnk->sym_cap) {
        return -1;
    }
    
    lnk->symbols[lnk->sym_count] = *sym;
    return lnk->sym_count++;
}

static int linker_find_symbol(LinkerState* lnk, const char* name) {
    for (uint32_t i = 0; i < lnk->sym_count; i++) {
        if (strcmp(lnk->symbols[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// OBJECT FILE LOADING
// ============================================================================

static int linker_load_object(LinkerState* lnk, const char* path) {
    int fd = mimic_fopen(path, MIMIC_FILE_READ);
    if (fd < 0) return fd;
    
    // Read header
    uint32_t header[4];
    int n = mimic_fread(fd, header, sizeof(header));
    if (n != sizeof(header)) {
        mimic_fclose(fd);
        return MIMIC_ERR_CORRUPT;
    }
    
    uint32_t code_size = header[0];
    uint32_t data_size = header[1];
    uint32_t reloc_count = header[2];
    uint32_t sym_count = header[3];
    
    // Record section offsets for relocation adjustment
    uint32_t text_offset = lnk->text_size;
    uint32_t data_offset = lnk->data_size;
    
    // Read and append code
    if (code_size > 0) {
        if (lnk->text_size + code_size > lnk->text_cap) {
            mimic_fclose(fd);
            return MIMIC_ERR_NOMEM;
        }
        
        n = mimic_fread(fd, lnk->text + lnk->text_size, code_size);
        if (n != (int)code_size) {
            mimic_fclose(fd);
            return MIMIC_ERR_IO;
        }
        lnk->text_size += code_size;
    }
    
    // Read and append data
    if (data_size > 0) {
        if (lnk->data_size + data_size > lnk->data_cap) {
            mimic_fclose(fd);
            return MIMIC_ERR_NOMEM;
        }
        
        n = mimic_fread(fd, lnk->data + lnk->data_size, data_size);
        if (n != (int)data_size) {
            mimic_fclose(fd);
            return MIMIC_ERR_IO;
        }
        lnk->data_size += data_size;
    }
    
    // Read relocations and adjust offsets
    for (uint32_t i = 0; i < reloc_count; i++) {
        MimiReloc reloc;
        n = mimic_fread(fd, &reloc, sizeof(reloc));
        if (n != sizeof(reloc)) {
            mimic_fclose(fd);
            return MIMIC_ERR_CORRUPT;
        }
        
        // Adjust offset based on section
        switch (reloc.section) {
            case MIMI_SECT_TEXT:
                reloc.offset += text_offset;
                break;
            case MIMI_SECT_DATA:
                reloc.offset += data_offset;
                break;
        }
        
        // Symbol index will be remapped after loading symbols
        // For now, store with original index
        
        if (lnk->reloc_count < lnk->reloc_cap) {
            lnk->relocs[lnk->reloc_count++] = reloc;
        }
    }
    
    // Read symbols and merge
    for (uint32_t i = 0; i < sym_count; i++) {
        MimiSymbol sym;
        n = mimic_fread(fd, &sym, sizeof(sym));
        if (n != sizeof(sym)) {
            mimic_fclose(fd);
            return MIMIC_ERR_CORRUPT;
        }
        
        // Adjust symbol value based on section
        switch (sym.section) {
            case MIMI_SECT_TEXT:
                sym.value += text_offset;
                break;
            case MIMI_SECT_DATA:
                sym.value += data_offset;
                break;
        }
        
        // Check for main entry point
        if (strcmp(sym.name, "main") == 0 && sym.type == MIMI_SYM_GLOBAL) {
            lnk->entry_offset = sym.value;
            lnk->has_entry = true;
        }
        
        linker_add_symbol(lnk, &sym);
    }
    
    mimic_fclose(fd);
    return MIMIC_OK;
}

// ============================================================================
// RELOCATION PROCESSING
// ============================================================================

static int linker_process_relocations(LinkerState* lnk) {
    // Now that all symbols are loaded, process relocations
    // Symbol indices in relocs reference the combined symbol table
    
    for (uint32_t i = 0; i < lnk->reloc_count; i++) {
        MimiReloc* reloc = &lnk->relocs[i];
        
        // The reloc's symbol_idx should reference our combined symbol table
        // In a real linker, we'd remap indices during object loading
        // For now, symbols are expected to be resolved by name
        
        if (reloc->symbol_idx >= lnk->sym_count) {
            snprintf(lnk->error_msg, sizeof(lnk->error_msg),
                     "Invalid symbol reference in relocation");
            lnk->error_count++;
            continue;
        }
        
        MimiSymbol* sym = &lnk->symbols[reloc->symbol_idx];
        
        // Check for unresolved symbols
        if (sym->type == MIMI_SYM_EXTERN && sym->section == MIMI_SECT_NULL) {
            // Unresolved external - check if it's a syscall
            if (sym->type != MIMI_SYM_SYSCALL) {
                snprintf(lnk->error_msg, sizeof(lnk->error_msg),
                         "Unresolved symbol: %s", sym->name);
                lnk->error_count++;
            }
        }
    }
    
    return (lnk->error_count > 0) ? MIMIC_ERR_NOENT : MIMIC_OK;
}

// ============================================================================
// BINARY OUTPUT
// ============================================================================

static int linker_write_binary(LinkerState* lnk, const char* output) {
    int fd = mimic_fopen(output, MIMIC_FILE_WRITE | MIMIC_FILE_CREATE | MIMIC_FILE_TRUNC);
    if (fd < 0) return fd;
    
    // Build header
    MimiHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    
    hdr.magic = MIMI_MAGIC;
    hdr.version = MIMI_VERSION;
    hdr.flags = 0;
    
#if MIMIC_TARGET_RP2350
    hdr.arch = MIMI_ARCH_CORTEX_M33;
#else
    hdr.arch = MIMI_ARCH_CORTEX_M0P;
#endif
    
    hdr.entry_offset = lnk->entry_offset;
    hdr.text_size = lnk->text_size;
    hdr.rodata_size = lnk->rodata_size;
    hdr.data_size = lnk->data_size;
    hdr.bss_size = lnk->bss_size;
    hdr.reloc_count = lnk->reloc_count;
    hdr.symbol_count = lnk->sym_count;
    hdr.stack_request = 4096;  // Default 4KB stack
    hdr.heap_request = 8192;   // Default 8KB heap
    
    strncpy(hdr.name, lnk->name, sizeof(hdr.name) - 1);
    
    // Write header
    mimic_fwrite(fd, &hdr, sizeof(hdr));
    
    // Write sections
    if (lnk->text_size > 0) {
        mimic_fwrite(fd, lnk->text, lnk->text_size);
    }
    
    if (lnk->rodata_size > 0) {
        mimic_fwrite(fd, lnk->rodata, lnk->rodata_size);
    }
    
    if (lnk->data_size > 0) {
        mimic_fwrite(fd, lnk->data, lnk->data_size);
    }
    
    // Write relocations
    if (lnk->reloc_count > 0) {
        mimic_fwrite(fd, lnk->relocs, lnk->reloc_count * sizeof(MimiReloc));
    }
    
    // Write symbols (optional, for debugging)
    if (lnk->sym_count > 0) {
        mimic_fwrite(fd, lnk->symbols, lnk->sym_count * sizeof(MimiSymbol));
    }
    
    mimic_fclose(fd);
    
    return MIMIC_OK;
}

// ============================================================================
// PUBLIC API
// ============================================================================

int mimic_cc_link(CompilerState* cc, const char** obj_files, uint32_t count,
                   const char* output) {
    // Initialize linker state
    LinkerState lnk = {0};
    lnk.cc = cc;
    
    // Allocate buffers
    lnk.text_cap = 64 * 1024;   // 64KB code
    lnk.text = mimic_kmalloc(lnk.text_cap);
    
    lnk.rodata_cap = 16 * 1024; // 16KB rodata
    lnk.rodata = mimic_kmalloc(lnk.rodata_cap);
    
    lnk.data_cap = 16 * 1024;   // 16KB data
    lnk.data = mimic_kmalloc(lnk.data_cap);
    
    lnk.reloc_cap = 512;
    lnk.relocs = mimic_kmalloc(lnk.reloc_cap * sizeof(MimiReloc));
    
    lnk.sym_cap = 256;
    lnk.symbols = mimic_kmalloc(lnk.sym_cap * sizeof(MimiSymbol));
    
    if (!lnk.text || !lnk.rodata || !lnk.data || !lnk.relocs || !lnk.symbols) {
        if (lnk.text) mimic_kfree(lnk.text);
        if (lnk.rodata) mimic_kfree(lnk.rodata);
        if (lnk.data) mimic_kfree(lnk.data);
        if (lnk.relocs) mimic_kfree(lnk.relocs);
        if (lnk.symbols) mimic_kfree(lnk.symbols);
        return MIMIC_ERR_NOMEM;
    }
    
    // Extract output name from path
    const char* name = strrchr(output, '/');
    name = name ? name + 1 : output;
    strncpy(lnk.name, name, sizeof(lnk.name) - 1);
    
    // Remove extension from name
    char* dot = strrchr(lnk.name, '.');
    if (dot) *dot = '\0';
    
    // Load all object files
    for (uint32_t i = 0; i < count; i++) {
        int err = linker_load_object(&lnk, obj_files[i]);
        if (err != MIMIC_OK) {
            if (cc->verbose) {
                printf("[LINK] Failed to load: %s (error %d)\n", obj_files[i], err);
            }
            mimic_kfree(lnk.text);
            mimic_kfree(lnk.rodata);
            mimic_kfree(lnk.data);
            mimic_kfree(lnk.relocs);
            mimic_kfree(lnk.symbols);
            return err;
        }
    }
    
    // Check for entry point
    if (!lnk.has_entry) {
        strncpy(cc->error_msg, "No entry point found (missing 'main')", 
                sizeof(cc->error_msg) - 1);
        cc->error_count++;
        
        mimic_kfree(lnk.text);
        mimic_kfree(lnk.rodata);
        mimic_kfree(lnk.data);
        mimic_kfree(lnk.relocs);
        mimic_kfree(lnk.symbols);
        return MIMIC_ERR_NOENT;
    }
    
    // Process relocations
    int err = linker_process_relocations(&lnk);
    if (err != MIMIC_OK && lnk.error_count > 0) {
        strncpy(cc->error_msg, lnk.error_msg, sizeof(cc->error_msg) - 1);
        cc->error_count += lnk.error_count;
        
        mimic_kfree(lnk.text);
        mimic_kfree(lnk.rodata);
        mimic_kfree(lnk.data);
        mimic_kfree(lnk.relocs);
        mimic_kfree(lnk.symbols);
        return err;
    }
    
    // Write output binary
    err = linker_write_binary(&lnk, output);
    
    if (cc->verbose) {
        printf("[LINK] Output: %s\n", output);
        printf("[LINK] .text:   %lu bytes\n", lnk.text_size);
        printf("[LINK] .rodata: %lu bytes\n", lnk.rodata_size);
        printf("[LINK] .data:   %lu bytes\n", lnk.data_size);
        printf("[LINK] .bss:    %lu bytes\n", lnk.bss_size);
        printf("[LINK] Entry:   0x%08lX\n", lnk.entry_offset);
    }
    
    // Cleanup
    mimic_kfree(lnk.text);
    mimic_kfree(lnk.rodata);
    mimic_kfree(lnk.data);
    mimic_kfree(lnk.relocs);
    mimic_kfree(lnk.symbols);
    
    return err;
}

// ============================================================================
// FULL COMPILATION PIPELINE
// ============================================================================

int mimic_cc_init(CompilerState* cc) {
    memset(cc, 0, sizeof(CompilerState));
    
    cc->io_buf_size = MIMIC_CC_IO_BUFFER;
    cc->io_buffer = mimic_kmalloc(cc->io_buf_size);
    
    if (!cc->io_buffer) {
        return MIMIC_ERR_NOMEM;
    }
    
    return MIMIC_OK;
}

void mimic_cc_cleanup(CompilerState* cc) {
    if (cc->io_buffer) {
        mimic_kfree(cc->io_buffer);
        cc->io_buffer = NULL;
    }
    if (cc->strings) {
        mimic_kfree(cc->strings);
        cc->strings = NULL;
    }
}

int mimic_cc_compile(CompilerState* cc, const char* source, const char* output) {
    int err;
    
    // Generate intermediate file paths
    snprintf(cc->tok_path, sizeof(cc->tok_path), "%s%s", MIMIC_CC_TMP_DIR, "/temp.tok");
    snprintf(cc->ast_path, sizeof(cc->ast_path), "%s%s", MIMIC_CC_TMP_DIR, "/temp.ast");
    snprintf(cc->ir_path, sizeof(cc->ir_path), "%s%s", MIMIC_CC_TMP_DIR, "/temp.ir");
    snprintf(cc->obj_path, sizeof(cc->obj_path), "%s%s", MIMIC_CC_TMP_DIR, "/temp.o");
    
    cc->verbose = true;  // For debugging
    
    printf("[CC] Pass 1: Lexer\n");
    err = mimic_cc_lex(cc, source, cc->tok_path);
    if (err != MIMIC_OK) {
        printf("[CC] Lexer failed: %d\n", err);
        return err;
    }
    
    printf("[CC] Pass 2: Parser\n");
    err = mimic_cc_parse(cc, cc->tok_path, cc->ast_path);
    if (err != MIMIC_OK) {
        printf("[CC] Parser failed: %d\n", err);
        return err;
    }
    
    // Skip semantic analysis for now (Pass 3)
    // In a full implementation, this would type-check and generate IR
    
    printf("[CC] Pass 4: Code Generation\n");
    err = mimic_cc_codegen(cc, cc->ast_path, cc->obj_path);
    if (err != MIMIC_OK) {
        printf("[CC] Codegen failed: %d\n", err);
        return err;
    }
    
    printf("[CC] Pass 5: Linker\n");
    const char* obj_files[] = { cc->obj_path };
    err = mimic_cc_link(cc, obj_files, 1, output);
    if (err != MIMIC_OK) {
        printf("[CC] Linker failed: %d\n", err);
        return err;
    }
    
    return MIMIC_OK;
}

const char* mimic_cc_error(CompilerState* cc) {
    return cc->error_msg;
}

void mimic_cc_print_errors(CompilerState* cc) {
    if (cc->error_count > 0) {
        printf("\nErrors (%lu):\n", cc->error_count);
        if (cc->error_line > 0) {
            printf("  Line %lu: %s\n", cc->error_line, cc->error_msg);
        } else {
            printf("  %s\n", cc->error_msg);
        }
    }
    if (cc->warning_count > 0) {
        printf("Warnings: %lu\n", cc->warning_count);
    }
}

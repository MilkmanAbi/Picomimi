/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC Code Generator - ARM Thumb Code Generation                         ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Pass 4: source.ir → source.o                                             ║
 * ║  Generates position-independent ARM Thumb code                            ║
 * ║  RAM usage: ~8-16KB (code buffer + current function state)                ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include <string.h>
#include <stdio.h>

#include "mimic.h"
#include "mimic_fat32.h"
#include "mimic_cc.h"

// ============================================================================
// ARM THUMB INSTRUCTION ENCODING
// ============================================================================

// Register names (for debugging)
static const char* reg_names[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "fp", "ip", "sp", "lr", "pc"
};

// Low registers (r0-r7) can be used with most Thumb instructions
#define REG_R0      0
#define REG_R1      1
#define REG_R2      2
#define REG_R3      3
#define REG_R4      4
#define REG_R5      5
#define REG_R6      6
#define REG_R7      7

// High registers and special
#define REG_R8      8
#define REG_R9      9
#define REG_R10     10
#define REG_FP      11      // Frame pointer (r11)
#define REG_IP      12      // Intra-procedure call scratch
#define REG_SP      13      // Stack pointer
#define REG_LR      14      // Link register
#define REG_PC      15      // Program counter

// Calling convention:
//   r0-r3: arguments and return value
//   r4-r7: callee-saved (must preserve)
//   sp: stack pointer
//   lr: link register

// ============================================================================
// THUMB INSTRUCTION ENCODERS
// ============================================================================

// Move immediate to register (MOV Rd, #imm8)
// Encoding: 001 00 Rd3 imm8
uint16_t thumb_mov_imm(uint8_t rd, uint8_t imm8) {
    return 0x2000 | ((rd & 7) << 8) | imm8;
}

// Move register to register (MOV Rd, Rm) - for low registers
// Encoding: 0100 0110 D Rm4 Rd3
uint16_t thumb_mov_reg(uint8_t rd, uint8_t rm) {
    if (rd < 8 && rm < 8) {
        // Use ADD Rd, Rm, #0 for low-low
        return 0x1C00 | (rm << 3) | rd;
    }
    // High register move
    uint8_t D = (rd >> 3) & 1;
    return 0x4600 | (D << 7) | ((rm & 0xF) << 3) | (rd & 7);
}

// Add registers (ADD Rd, Rn, Rm)
// Encoding: 0001 100 Rm3 Rn3 Rd3
uint16_t thumb_add_reg(uint8_t rd, uint8_t rn, uint8_t rm) {
    return 0x1800 | ((rm & 7) << 6) | ((rn & 7) << 3) | (rd & 7);
}

// Add immediate (ADD Rd, Rn, #imm3)
// Encoding: 0001 110 imm3 Rn3 Rd3
uint16_t thumb_add_imm3(uint8_t rd, uint8_t rn, uint8_t imm3) {
    return 0x1C00 | ((imm3 & 7) << 6) | ((rn & 7) << 3) | (rd & 7);
}

// Add immediate to register (ADD Rd, #imm8)
// Encoding: 0011 0 Rd3 imm8
uint16_t thumb_add_imm8(uint8_t rd, uint8_t imm8) {
    return 0x3000 | ((rd & 7) << 8) | imm8;
}

// Subtract registers (SUB Rd, Rn, Rm)
// Encoding: 0001 101 Rm3 Rn3 Rd3
uint16_t thumb_sub_reg(uint8_t rd, uint8_t rn, uint8_t rm) {
    return 0x1A00 | ((rm & 7) << 6) | ((rn & 7) << 3) | (rd & 7);
}

// Subtract immediate (SUB Rd, Rn, #imm3)
// Encoding: 0001 111 imm3 Rn3 Rd3
uint16_t thumb_sub_imm3(uint8_t rd, uint8_t rn, uint8_t imm3) {
    return 0x1E00 | ((imm3 & 7) << 6) | ((rn & 7) << 3) | (rd & 7);
}

// Subtract immediate from register (SUB Rd, #imm8)
// Encoding: 0011 1 Rd3 imm8
uint16_t thumb_sub_imm8(uint8_t rd, uint8_t imm8) {
    return 0x3800 | ((rd & 7) << 8) | imm8;
}

// Compare registers (CMP Rn, Rm)
// Encoding: 0100 0010 10 Rm3 Rn3
uint16_t thumb_cmp_reg(uint8_t rn, uint8_t rm) {
    return 0x4280 | ((rm & 7) << 3) | (rn & 7);
}

// Compare immediate (CMP Rn, #imm8)
// Encoding: 0010 1 Rn3 imm8
uint16_t thumb_cmp_imm(uint8_t rn, uint8_t imm8) {
    return 0x2800 | ((rn & 7) << 8) | imm8;
}

// Load from SP+offset (LDR Rd, [SP, #imm8*4])
// Encoding: 1001 1 Rd3 imm8
uint16_t thumb_ldr_sp(uint8_t rd, uint8_t offset) {
    return 0x9800 | ((rd & 7) << 8) | (offset & 0xFF);
}

// Store to SP+offset (STR Rd, [SP, #imm8*4])
// Encoding: 1001 0 Rd3 imm8
uint16_t thumb_str_sp(uint8_t rd, uint8_t offset) {
    return 0x9000 | ((rd & 7) << 8) | (offset & 0xFF);
}

// Load from register+offset (LDR Rd, [Rn, #imm5*4])
// Encoding: 0110 1 imm5 Rn3 Rd3
uint16_t thumb_ldr_imm(uint8_t rd, uint8_t rn, uint8_t imm5) {
    return 0x6800 | ((imm5 & 0x1F) << 6) | ((rn & 7) << 3) | (rd & 7);
}

// Store to register+offset (STR Rd, [Rn, #imm5*4])
// Encoding: 0110 0 imm5 Rn3 Rd3
uint16_t thumb_str_imm(uint8_t rd, uint8_t rn, uint8_t imm5) {
    return 0x6000 | ((imm5 & 0x1F) << 6) | ((rn & 7) << 3) | (rd & 7);
}

// Load byte (LDRB Rd, [Rn, #imm5])
// Encoding: 0111 1 imm5 Rn3 Rd3
uint16_t thumb_ldrb_imm(uint8_t rd, uint8_t rn, uint8_t imm5) {
    return 0x7800 | ((imm5 & 0x1F) << 6) | ((rn & 7) << 3) | (rd & 7);
}

// Store byte (STRB Rd, [Rn, #imm5])
// Encoding: 0111 0 imm5 Rn3 Rd3
uint16_t thumb_strb_imm(uint8_t rd, uint8_t rn, uint8_t imm5) {
    return 0x7000 | ((imm5 & 0x1F) << 6) | ((rn & 7) << 3) | (rd & 7);
}

// Push registers
// Encoding: 1011 010 R reglist8
uint16_t thumb_push(uint16_t regmask) {
    uint8_t R = (regmask & (1 << REG_LR)) ? 1 : 0;
    return 0xB400 | (R << 8) | (regmask & 0xFF);
}

// Pop registers
// Encoding: 1011 110 P reglist8
uint16_t thumb_pop(uint16_t regmask) {
    uint8_t P = (regmask & (1 << REG_PC)) ? 1 : 0;
    return 0xBC00 | (P << 8) | (regmask & 0xFF);
}

// Unconditional branch (B offset)
// Encoding: 1110 0 offset11
uint16_t thumb_b(int16_t offset) {
    // offset is in bytes, divide by 2 for encoding
    return 0xE000 | ((offset >> 1) & 0x7FF);
}

// Branch with link (BL offset) - 32-bit instruction
// Encoding: 11110 S imm10 : 11 J1 1 J2 imm11
uint32_t thumb_bl(int32_t offset) {
    // offset is in bytes, divide by 2 for encoding
    offset >>= 1;
    
    uint32_t s = (offset >> 24) & 1;
    uint32_t i1 = (offset >> 23) & 1;
    uint32_t i2 = (offset >> 22) & 1;
    uint32_t imm10 = (offset >> 11) & 0x3FF;
    uint32_t imm11 = offset & 0x7FF;
    
    uint32_t j1 = (~i1 ^ s) & 1;
    uint32_t j2 = (~i2 ^ s) & 1;
    
    uint16_t hi = 0xF000 | (s << 10) | imm10;
    uint16_t lo = 0xD000 | (j1 << 13) | (j2 << 11) | imm11;
    
    return (hi << 16) | lo;
}

// Branch and exchange (BX Rm)
// Encoding: 0100 0111 0 Rm4 000
uint16_t thumb_bx(uint8_t rm) {
    return 0x4700 | ((rm & 0xF) << 3);
}

// Conditional branches
// Encoding: 1101 cond offset8
uint16_t thumb_beq(int8_t offset) { return 0xD000 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bne(int8_t offset) { return 0xD100 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bcs(int8_t offset) { return 0xD200 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bcc(int8_t offset) { return 0xD300 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bmi(int8_t offset) { return 0xD400 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bpl(int8_t offset) { return 0xD500 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bvs(int8_t offset) { return 0xD600 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bvc(int8_t offset) { return 0xD700 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bhi(int8_t offset) { return 0xD800 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bls(int8_t offset) { return 0xD900 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bge(int8_t offset) { return 0xDA00 | ((offset >> 1) & 0xFF); }
uint16_t thumb_blt(int8_t offset) { return 0xDB00 | ((offset >> 1) & 0xFF); }
uint16_t thumb_bgt(int8_t offset) { return 0xDC00 | ((offset >> 1) & 0xFF); }
uint16_t thumb_ble(int8_t offset) { return 0xDD00 | ((offset >> 1) & 0xFF); }

// ALU operations between registers
// Encoding: 0100 00 op4 Rm3 Rd3
uint16_t thumb_and(uint8_t rd, uint8_t rm) { return 0x4000 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_eor(uint8_t rd, uint8_t rm) { return 0x4040 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_lsl(uint8_t rd, uint8_t rm) { return 0x4080 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_lsr(uint8_t rd, uint8_t rm) { return 0x40C0 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_asr(uint8_t rd, uint8_t rm) { return 0x4100 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_adc(uint8_t rd, uint8_t rm) { return 0x4140 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_sbc(uint8_t rd, uint8_t rm) { return 0x4180 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_ror(uint8_t rd, uint8_t rm) { return 0x41C0 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_tst(uint8_t rn, uint8_t rm) { return 0x4200 | ((rm & 7) << 3) | (rn & 7); }
uint16_t thumb_neg(uint8_t rd, uint8_t rm) { return 0x4240 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_cmn(uint8_t rn, uint8_t rm) { return 0x42C0 | ((rm & 7) << 3) | (rn & 7); }
uint16_t thumb_orr(uint8_t rd, uint8_t rm) { return 0x4300 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_mul(uint8_t rd, uint8_t rm) { return 0x4340 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_bic(uint8_t rd, uint8_t rm) { return 0x4380 | ((rm & 7) << 3) | (rd & 7); }
uint16_t thumb_mvn(uint8_t rd, uint8_t rm) { return 0x43C0 | ((rm & 7) << 3) | (rd & 7); }

// Add SP immediate (ADD SP, #imm7*4)
// Encoding: 1011 0000 0 imm7
uint16_t thumb_add_sp_imm(uint8_t imm7) {
    return 0xB000 | (imm7 & 0x7F);
}

// Subtract SP immediate (SUB SP, #imm7*4)
// Encoding: 1011 0000 1 imm7
uint16_t thumb_sub_sp_imm(uint8_t imm7) {
    return 0xB080 | (imm7 & 0x7F);
}

// Software interrupt (SVC #imm8)
// Encoding: 1101 1111 imm8
uint16_t thumb_svc(uint8_t imm8) {
    return 0xDF00 | imm8;
}

// No operation
uint16_t thumb_nop(void) {
    return 0xBF00;
}

// ============================================================================
// CODE GENERATOR STATE
// ============================================================================

typedef struct {
    // Output buffers
    uint8_t*        code;
    uint32_t        code_size;
    uint32_t        code_cap;
    
    uint8_t*        data;
    uint32_t        data_size;
    uint32_t        data_cap;
    
    // Relocations
    MimiReloc*      relocs;
    uint32_t        reloc_count;
    uint32_t        reloc_cap;
    
    // Symbols
    MimiSymbol*     symbols;
    uint32_t        sym_count;
    uint32_t        sym_cap;
    
    // Register allocation
    uint8_t         reg_used;       // Bitmask of registers in use
    
    // Stack frame
    int32_t         stack_offset;
    int32_t         local_size;
    int32_t         frame_size;
    
    // Labels for branching
    uint32_t*       labels;
    uint32_t        label_count;
    uint32_t        label_cap;
    
    // Pending branches (need fixup)
    struct {
        uint32_t    code_offset;
        uint32_t    label_id;
        uint8_t     type;           // 0=B, 1=Bcc, 2=BL
    }*              branches;
    uint32_t        branch_count;
    uint32_t        branch_cap;
    
    // Current function
    uint32_t        func_start;
    
    // Compiler state
    CompilerState*  cc;
} CodeGenState;

// ============================================================================
// CODE GENERATION HELPERS
// ============================================================================

static void cg_emit16(CodeGenState* cg, uint16_t instr) {
    if (cg->code_size + 2 > cg->code_cap) {
        // Would need to grow buffer - for now, just fail
        return;
    }
    
    cg->code[cg->code_size++] = instr & 0xFF;
    cg->code[cg->code_size++] = (instr >> 8) & 0xFF;
}

static void cg_emit32(CodeGenState* cg, uint32_t instr) {
    // Thumb-2 32-bit instructions are stored high halfword first
    cg_emit16(cg, instr >> 16);
    cg_emit16(cg, instr & 0xFFFF);
}

static uint8_t cg_alloc_reg(CodeGenState* cg) {
    for (int i = 0; i < 8; i++) {
        if (!(cg->reg_used & (1 << i))) {
            cg->reg_used |= (1 << i);
            return i;
        }
    }
    // No register available - would need to spill
    return 0xFF;
}

static void cg_free_reg(CodeGenState* cg, uint8_t reg) {
    if (reg < 8) {
        cg->reg_used &= ~(1 << reg);
    }
}

static uint32_t cg_label(CodeGenState* cg) {
    if (cg->label_count >= cg->label_cap) {
        return 0xFFFFFFFF;
    }
    uint32_t id = cg->label_count++;
    cg->labels[id] = 0xFFFFFFFF;  // Undefined
    return id;
}

static void cg_bind_label(CodeGenState* cg, uint32_t label) {
    if (label < cg->label_cap) {
        cg->labels[label] = cg->code_size;
    }
}

static void cg_branch(CodeGenState* cg, uint32_t label, uint8_t type) {
    if (cg->branch_count >= cg->branch_cap) return;
    
    cg->branches[cg->branch_count].code_offset = cg->code_size;
    cg->branches[cg->branch_count].label_id = label;
    cg->branches[cg->branch_count].type = type;
    cg->branch_count++;
    
    // Emit placeholder
    if (type == 2) {
        cg_emit32(cg, 0);  // BL is 32-bit
    } else {
        cg_emit16(cg, 0);
    }
}

static void cg_fixup_branches(CodeGenState* cg) {
    for (uint32_t i = 0; i < cg->branch_count; i++) {
        uint32_t offset = cg->branches[i].code_offset;
        uint32_t label = cg->branches[i].label_id;
        uint8_t type = cg->branches[i].type;
        
        if (label >= cg->label_count || cg->labels[label] == 0xFFFFFFFF) {
            continue;  // Unresolved label
        }
        
        int32_t target = cg->labels[label];
        int32_t rel = target - (offset + 4);  // PC is 4 ahead
        
        if (type == 2) {
            // BL
            uint32_t instr = thumb_bl(rel);
            cg->code[offset] = (instr >> 16) & 0xFF;
            cg->code[offset + 1] = (instr >> 24) & 0xFF;
            cg->code[offset + 2] = instr & 0xFF;
            cg->code[offset + 3] = (instr >> 8) & 0xFF;
        } else if (type == 0) {
            // B
            uint16_t instr = thumb_b(rel);
            cg->code[offset] = instr & 0xFF;
            cg->code[offset + 1] = (instr >> 8) & 0xFF;
        } else {
            // Bcc - limited range
            uint16_t instr = cg->code[offset] | (cg->code[offset + 1] << 8);
            instr = (instr & 0xFF00) | ((rel >> 1) & 0xFF);
            cg->code[offset] = instr & 0xFF;
            cg->code[offset + 1] = (instr >> 8) & 0xFF;
        }
    }
}

// ============================================================================
// FUNCTION PROLOGUE/EPILOGUE
// ============================================================================

static void cg_function_prologue(CodeGenState* cg, int32_t locals_size) {
    cg->func_start = cg->code_size;
    cg->local_size = (locals_size + 3) & ~3;  // Align to 4 bytes
    
    // Push LR and callee-saved registers (r4-r7)
    cg_emit16(cg, thumb_push((1 << REG_LR) | 0xF0));  // push {r4-r7, lr}
    
    // Allocate stack space for locals
    if (cg->local_size > 0) {
        int32_t words = cg->local_size / 4;
        while (words > 127) {
            cg_emit16(cg, thumb_sub_sp_imm(127));
            words -= 127;
        }
        if (words > 0) {
            cg_emit16(cg, thumb_sub_sp_imm(words));
        }
    }
    
    cg->stack_offset = cg->local_size;
}

static void cg_function_epilogue(CodeGenState* cg) {
    // Deallocate stack space
    if (cg->local_size > 0) {
        int32_t words = cg->local_size / 4;
        while (words > 127) {
            cg_emit16(cg, thumb_add_sp_imm(127));
            words -= 127;
        }
        if (words > 0) {
            cg_emit16(cg, thumb_add_sp_imm(words));
        }
    }
    
    // Pop and return
    cg_emit16(cg, thumb_pop((1 << REG_PC) | 0xF0));  // pop {r4-r7, pc}
}

// ============================================================================
// SYSCALL GENERATION
// ============================================================================

static void cg_syscall(CodeGenState* cg, uint8_t syscall_num) {
    // Move syscall number to r7 (ARM convention)
    cg_emit16(cg, thumb_mov_imm(REG_R7, syscall_num));
    // Execute SVC
    cg_emit16(cg, thumb_svc(0));
}

// ============================================================================
// IR TO THUMB TRANSLATION
// ============================================================================

int codegen_ir_to_thumb(CodeGen* gen, const DiskIR* ir, uint32_t count) {
    // This would be the main IR translation loop
    // For now, just a skeleton
    
    for (uint32_t i = 0; i < count; i++) {
        const DiskIR* instr = &ir[i];
        
        switch (instr->opcode) {
            case IR_CONST:
                // Load constant into destination register
                break;
                
            case IR_LOAD:
                // Load from memory
                break;
                
            case IR_STORE:
                // Store to memory
                break;
                
            case IR_ADD:
            case IR_SUB:
            case IR_MUL:
                // Arithmetic
                break;
                
            case IR_JMP:
            case IR_JZ:
            case IR_JNZ:
                // Branching
                break;
                
            case IR_CALL:
                // Function call
                break;
                
            case IR_RET:
                // Return
                break;
                
            default:
                break;
        }
    }
    
    return MIMIC_OK;
}

// ============================================================================
// PUBLIC API
// ============================================================================

int mimic_cc_codegen(CompilerState* cc, const char* ir_input, const char* obj_output) {
    // Allocate code generator state
    CodeGenState cg = {0};
    cg.cc = cc;
    
    cg.code_cap = 32 * 1024;  // 32KB code buffer
    cg.code = mimic_kmalloc(cg.code_cap);
    
    cg.data_cap = 8 * 1024;   // 8KB data buffer
    cg.data = mimic_kmalloc(cg.data_cap);
    
    cg.reloc_cap = 256;
    cg.relocs = mimic_kmalloc(cg.reloc_cap * sizeof(MimiReloc));
    
    cg.sym_cap = 128;
    cg.symbols = mimic_kmalloc(cg.sym_cap * sizeof(MimiSymbol));
    
    cg.label_cap = 256;
    cg.labels = mimic_kmalloc(cg.label_cap * sizeof(uint32_t));
    
    cg.branch_cap = 256;
    cg.branches = mimic_kmalloc(cg.branch_cap * sizeof(*cg.branches));
    
    if (!cg.code || !cg.data || !cg.relocs || !cg.symbols || 
        !cg.labels || !cg.branches) {
        // Cleanup and return error
        if (cg.code) mimic_kfree(cg.code);
        if (cg.data) mimic_kfree(cg.data);
        if (cg.relocs) mimic_kfree(cg.relocs);
        if (cg.symbols) mimic_kfree(cg.symbols);
        if (cg.labels) mimic_kfree(cg.labels);
        if (cg.branches) mimic_kfree(cg.branches);
        return MIMIC_ERR_NOMEM;
    }
    
    // TODO: Read IR file and generate code
    // For now, just create a minimal stub
    
    // Example: Generate a simple function that returns 42
    cg_function_prologue(&cg, 0);
    cg_emit16(&cg, thumb_mov_imm(REG_R0, 42));
    cg_function_epilogue(&cg);
    
    // Fixup branches
    cg_fixup_branches(&cg);
    
    // Write object file
    int fd = mimic_fopen(obj_output, MIMIC_FILE_WRITE | MIMIC_FILE_CREATE | MIMIC_FILE_TRUNC);
    if (fd < 0) {
        mimic_kfree(cg.code);
        mimic_kfree(cg.data);
        mimic_kfree(cg.relocs);
        mimic_kfree(cg.symbols);
        mimic_kfree(cg.labels);
        mimic_kfree(cg.branches);
        return fd;
    }
    
    // Write header
    uint32_t header[4] = {
        cg.code_size,
        cg.data_size,
        cg.reloc_count,
        cg.sym_count
    };
    mimic_fwrite(fd, header, sizeof(header));
    
    // Write code
    mimic_fwrite(fd, cg.code, cg.code_size);
    
    // Write data
    if (cg.data_size > 0) {
        mimic_fwrite(fd, cg.data, cg.data_size);
    }
    
    // Write relocations
    if (cg.reloc_count > 0) {
        mimic_fwrite(fd, cg.relocs, cg.reloc_count * sizeof(MimiReloc));
    }
    
    // Write symbols
    if (cg.sym_count > 0) {
        mimic_fwrite(fd, cg.symbols, cg.sym_count * sizeof(MimiSymbol));
    }
    
    mimic_fclose(fd);
    
    // Update compiler state
    cc->code_bytes = cg.code_size;
    
    if (cc->verbose) {
        printf("[CODEGEN] %lu bytes code, %lu relocations\n", 
               cg.code_size, cg.reloc_count);
    }
    
    // Cleanup
    mimic_kfree(cg.code);
    mimic_kfree(cg.data);
    mimic_kfree(cg.relocs);
    mimic_kfree(cg.symbols);
    mimic_kfree(cg.labels);
    mimic_kfree(cg.branches);
    
    return MIMIC_OK;
}

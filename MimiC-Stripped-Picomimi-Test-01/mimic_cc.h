/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC Compiler - Disk-Buffered C Compilation                             ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Based on TCC (Tiny C Compiler) architecture                              ║
 * ║  Multi-pass, disk-buffered for low memory footprint                       ║
 * ║  Full C98 support - trades speed for memory                               ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 * 
 * COMPILATION PASSES (all disk-buffered):
 *   Pass 1: LEXER      - source.c → source.tok    (~2-4KB RAM)
 *   Pass 2: PARSER     - source.tok → source.ast  (~8-16KB RAM)
 *   Pass 3: SEMANTIC   - source.ast → source.ir   (~16-32KB RAM)
 *   Pass 4: CODEGEN    - source.ir → source.o     (~8-16KB RAM)
 *   Pass 5: LINKER     - source.o + libs → source.mimi (~16-32KB RAM)
 * 
 * Each pass reads from disk, processes with minimal RAM, writes back to disk.
 * SD card becomes working memory - slow but allows full C compilation.
 */

#ifndef MIMIC_CC_H
#define MIMIC_CC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "mimic.h"
#include "mimic_fat32.h"

// ============================================================================
// TOKEN DEFINITIONS
// ============================================================================

typedef enum {
    // End of file
    TOK_EOF = 0,
    
    // Literals
    TOK_NUM,            // Integer constant
    TOK_FNUM,           // Float constant
    TOK_STR,            // String literal
    TOK_CHAR,           // Character literal
    TOK_IDENT,          // Identifier
    
    // Keywords
    TOK_AUTO,
    TOK_BREAK,
    TOK_CASE,
    TOK_CHAR_KW,
    TOK_CONST,
    TOK_CONTINUE,
    TOK_DEFAULT,
    TOK_DO,
    TOK_DOUBLE,
    TOK_ELSE,
    TOK_ENUM,
    TOK_EXTERN,
    TOK_FLOAT,
    TOK_FOR,
    TOK_GOTO,
    TOK_IF,
    TOK_INT,
    TOK_LONG,
    TOK_REGISTER,
    TOK_RETURN,
    TOK_SHORT,
    TOK_SIGNED,
    TOK_SIZEOF,
    TOK_STATIC,
    TOK_STRUCT,
    TOK_SWITCH,
    TOK_TYPEDEF,
    TOK_UNION,
    TOK_UNSIGNED,
    TOK_VOID,
    TOK_VOLATILE,
    TOK_WHILE,
    
    // Operators
    TOK_PLUS,           // +
    TOK_MINUS,          // -
    TOK_STAR,           // *
    TOK_SLASH,          // /
    TOK_PERCENT,        // %
    TOK_AMP,            // &
    TOK_PIPE,           // |
    TOK_CARET,          // ^
    TOK_TILDE,          // ~
    TOK_BANG,           // !
    TOK_QUESTION,       // ?
    TOK_COLON,          // :
    TOK_SEMICOLON,      // ;
    TOK_COMMA,          // ,
    TOK_DOT,            // .
    TOK_ARROW,          // ->
    TOK_LPAREN,         // (
    TOK_RPAREN,         // )
    TOK_LBRACKET,       // [
    TOK_RBRACKET,       // ]
    TOK_LBRACE,         // {
    TOK_RBRACE,         // }
    
    // Compound operators
    TOK_EQ,             // ==
    TOK_NE,             // !=
    TOK_LT,             // <
    TOK_GT,             // >
    TOK_LE,             // <=
    TOK_GE,             // >=
    TOK_AND,            // &&
    TOK_OR,             // ||
    TOK_SHL,            // <<
    TOK_SHR,            // >>
    TOK_INC,            // ++
    TOK_DEC,            // --
    
    // Assignment operators
    TOK_ASSIGN,         // =
    TOK_PLUS_ASSIGN,    // +=
    TOK_MINUS_ASSIGN,   // -=
    TOK_STAR_ASSIGN,    // *=
    TOK_SLASH_ASSIGN,   // /=
    TOK_PERCENT_ASSIGN, // %=
    TOK_AMP_ASSIGN,     // &=
    TOK_PIPE_ASSIGN,    // |=
    TOK_CARET_ASSIGN,   // ^=
    TOK_SHL_ASSIGN,     // <<=
    TOK_SHR_ASSIGN,     // >>=
    
    // Preprocessor (handled in lexer)
    TOK_PP_DEFINE,
    TOK_PP_INCLUDE,
    TOK_PP_IFDEF,
    TOK_PP_IFNDEF,
    TOK_PP_ELSE,
    TOK_PP_ENDIF,
    TOK_PP_PRAGMA,
    
    TOK_COUNT
} TokenType;

// Token on disk (8 bytes)
typedef struct __attribute__((packed)) {
    uint16_t    type;           // TokenType
    uint16_t    flags;          // Token flags
    uint32_t    value;          // Integer value, or string table offset
} DiskToken;

// Token with source info (in memory during lexing)
typedef struct {
    TokenType   type;
    uint32_t    value;
    uint32_t    line;
    uint32_t    col;
    char        text[64];       // For identifiers/strings
} Token;

// ============================================================================
// AST NODE TYPES
// ============================================================================

typedef enum {
    // Expressions
    AST_NUM,            // Integer literal
    AST_STR,            // String literal
    AST_IDENT,          // Identifier
    AST_BINOP,          // Binary operation
    AST_UNOP,           // Unary operation
    AST_CALL,           // Function call
    AST_INDEX,          // Array index
    AST_MEMBER,         // Struct member access
    AST_CAST,           // Type cast
    AST_SIZEOF,         // sizeof operator
    AST_COND,           // Ternary ?: operator
    AST_ASSIGN,         // Assignment
    AST_COMMA,          // Comma operator
    
    // Statements
    AST_BLOCK,          // Compound statement
    AST_IF,             // if statement
    AST_WHILE,          // while loop
    AST_DO,             // do-while loop
    AST_FOR,            // for loop
    AST_SWITCH,         // switch statement
    AST_CASE,           // case label
    AST_DEFAULT,        // default label
    AST_BREAK,          // break
    AST_CONTINUE,       // continue
    AST_RETURN,         // return
    AST_GOTO,           // goto
    AST_LABEL,          // label
    AST_EXPR_STMT,      // Expression statement
    
    // Declarations
    AST_VAR_DECL,       // Variable declaration
    AST_FUNC_DECL,      // Function declaration
    AST_FUNC_DEF,       // Function definition
    AST_STRUCT_DEF,     // Struct definition
    AST_ENUM_DEF,       // Enum definition
    AST_TYPEDEF,        // Typedef
    AST_PARAM,          // Function parameter
    
    // Top level
    AST_PROGRAM,        // Translation unit
    
    AST_COUNT
} ASTNodeType;

// AST node on disk (variable size, serialized)
typedef struct __attribute__((packed)) {
    uint8_t     type;           // ASTNodeType
    uint8_t     flags;
    uint16_t    child_count;
    uint32_t    data;           // Type-specific data
    // Followed by: child offsets (uint32_t each)
} DiskASTNode;

// ============================================================================
// TYPE SYSTEM
// ============================================================================

typedef enum {
    TYPE_VOID,
    TYPE_CHAR,
    TYPE_SHORT,
    TYPE_INT,
    TYPE_LONG,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_UNION,
    TYPE_ENUM,
    TYPE_FUNC,
} TypeKind;

typedef struct Type {
    TypeKind        kind;
    uint32_t        size;           // Size in bytes
    uint32_t        align;          // Alignment
    bool            is_unsigned;
    bool            is_const;
    bool            is_volatile;
    struct Type*    base;           // For pointers/arrays
    uint32_t        array_size;     // For arrays
    uint32_t        struct_id;      // For structs/unions
    uint32_t        param_count;    // For functions
} Type;

// ============================================================================
// SYMBOL TABLE
// ============================================================================

typedef enum {
    SYM_VAR,            // Variable
    SYM_FUNC,           // Function
    SYM_PARAM,          // Parameter
    SYM_TYPEDEF,        // Type alias
    SYM_STRUCT,         // Struct/union tag
    SYM_ENUM,           // Enum tag
    SYM_ENUM_CONST,     // Enum constant
    SYM_LABEL,          // Goto label
} SymbolKind;

typedef struct Symbol {
    char            name[32];
    SymbolKind      kind;
    Type*           type;
    uint32_t        offset;         // Stack offset or global address
    uint32_t        scope;          // Scope level
    bool            is_global;
    bool            is_extern;
    bool            is_static;
    bool            is_defined;     // For functions
    struct Symbol*  next;           // Hash chain
} Symbol;

// Symbol table
#define SYMTAB_SIZE     256         // Hash table size

typedef struct {
    Symbol*     buckets[SYMTAB_SIZE];
    uint32_t    scope_level;
    uint32_t    symbol_count;
} SymbolTable;

// ============================================================================
// INTERMEDIATE REPRESENTATION
// ============================================================================

typedef enum {
    // Constants
    IR_CONST,           // Load constant
    IR_ADDR,            // Load address
    
    // Memory
    IR_LOAD,            // Load from address
    IR_STORE,           // Store to address
    IR_ALLOCA,          // Stack allocation
    
    // Arithmetic
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_MOD,
    IR_NEG,
    
    // Bitwise
    IR_AND,
    IR_OR,
    IR_XOR,
    IR_NOT,
    IR_SHL,
    IR_SHR,
    
    // Comparison
    IR_EQ,
    IR_NE,
    IR_LT,
    IR_LE,
    IR_GT,
    IR_GE,
    
    // Control flow
    IR_JMP,             // Unconditional jump
    IR_JZ,              // Jump if zero
    IR_JNZ,             // Jump if not zero
    IR_CALL,            // Function call
    IR_RET,             // Return
    IR_LABEL,           // Label definition
    
    // Function
    IR_FUNC_BEGIN,      // Function prologue
    IR_FUNC_END,        // Function epilogue
    IR_PARAM,           // Function parameter
    IR_ARG,             // Call argument
    
    // Type conversion
    IR_CAST,            // Type cast
    IR_EXTEND,          // Sign extend
    IR_TRUNC,           // Truncate
    
    IR_COUNT
} IROpcode;

// IR instruction on disk (16 bytes)
typedef struct __attribute__((packed)) {
    uint8_t     opcode;         // IROpcode
    uint8_t     flags;
    uint8_t     size;           // Operand size (1, 2, 4)
    uint8_t     _pad;
    uint32_t    dest;           // Destination (register/stack slot)
    uint32_t    src1;           // Source 1
    uint32_t    src2;           // Source 2 / immediate
} DiskIR;

// ============================================================================
// CODE GENERATOR STATE
// ============================================================================

typedef struct {
    // Output
    uint8_t*    code;           // Code buffer
    uint32_t    code_size;
    uint32_t    code_capacity;
    
    uint8_t*    data;           // Data buffer
    uint32_t    data_size;
    uint32_t    data_capacity;
    
    // Relocations
    MimiReloc*  relocs;
    uint32_t    reloc_count;
    uint32_t    reloc_capacity;
    
    // Symbols
    MimiSymbol* symbols;
    uint32_t    symbol_count;
    uint32_t    symbol_capacity;
    
    // Register allocation
    uint8_t     reg_in_use;     // Bitmask of used registers
    int32_t     stack_offset;   // Current stack offset
    
    // Current function
    uint32_t    func_start;     // Start of current function
    uint32_t    local_size;     // Local variables size
} CodeGen;

// ============================================================================
// COMPILER STATE
// ============================================================================

typedef struct {
    // Input
    const char* source_path;
    const char* output_path;
    
    // Intermediate files
    char        tok_path[128];
    char        ast_path[128];
    char        ir_path[128];
    char        obj_path[128];
    
    // I/O buffers
    uint8_t*    io_buffer;
    uint32_t    io_buf_size;
    
    // Streams
    MimicStream in_stream;
    MimicStream out_stream;
    
    // Symbol table (disk-paged)
    SymbolTable symtab;
    
    // String table (for literals)
    char*       strings;
    uint32_t    string_size;
    uint32_t    string_capacity;
    
    // Error handling
    uint32_t    error_count;
    uint32_t    warning_count;
    char        error_msg[256];
    uint32_t    error_line;
    
    // Statistics
    uint32_t    tokens_processed;
    uint32_t    nodes_created;
    uint32_t    ir_instructions;
    uint32_t    code_bytes;
    
    // Options
    bool        optimize;
    bool        debug_info;
    bool        verbose;
} CompilerState;

// ============================================================================
// COMPILER API
// ============================================================================

// Initialize compiler
int mimic_cc_init(CompilerState* cc);

// Cleanup
void mimic_cc_cleanup(CompilerState* cc);

// Full compilation pipeline
int mimic_cc_compile(CompilerState* cc, const char* source, const char* output);

// Individual passes (for debugging/testing)
int mimic_cc_lex(CompilerState* cc, const char* source, const char* tok_output);
int mimic_cc_parse(CompilerState* cc, const char* tok_input, const char* ast_output);
int mimic_cc_semantic(CompilerState* cc, const char* ast_input, const char* ir_output);
int mimic_cc_codegen(CompilerState* cc, const char* ir_input, const char* obj_output);
int mimic_cc_link(CompilerState* cc, const char** obj_files, uint32_t count,
                   const char* output);

// Error reporting
const char* mimic_cc_error(CompilerState* cc);
void mimic_cc_print_errors(CompilerState* cc);

// ============================================================================
// LEXER
// ============================================================================

// Lexer state
typedef struct {
    MimicStream*    stream;
    int             current_char;
    int             peek_char;
    uint32_t        line;
    uint32_t        col;
    Token           current_token;
} Lexer;

int lexer_init(Lexer* lex, MimicStream* stream);
int lexer_next(Lexer* lex);
int lexer_peek(Lexer* lex);

// ============================================================================
// PARSER
// ============================================================================

typedef struct {
    MimicStream*    in_stream;      // Token input
    MimicStream*    out_stream;     // AST output
    DiskToken       current;
    DiskToken       peek;
    uint32_t        node_count;
    CompilerState*  cc;
} Parser;

int parser_init(Parser* p, MimicStream* in, MimicStream* out, CompilerState* cc);
int parser_parse(Parser* p);

// ============================================================================
// THUMB CODE GENERATION
// ============================================================================

// ARM Thumb instruction encoding helpers
uint16_t thumb_mov_imm(uint8_t rd, uint8_t imm8);
uint16_t thumb_add_reg(uint8_t rd, uint8_t rn, uint8_t rm);
uint16_t thumb_sub_reg(uint8_t rd, uint8_t rn, uint8_t rm);
uint16_t thumb_ldr_sp(uint8_t rd, uint8_t offset);
uint16_t thumb_str_sp(uint8_t rd, uint8_t offset);
uint16_t thumb_push(uint16_t regmask);
uint16_t thumb_pop(uint16_t regmask);
uint16_t thumb_b(int16_t offset);
uint32_t thumb_bl(int32_t offset);
uint16_t thumb_bx(uint8_t rm);
uint16_t thumb_cmp_reg(uint8_t rn, uint8_t rm);
uint16_t thumb_beq(int8_t offset);
uint16_t thumb_bne(int8_t offset);

// Generate Thumb code from IR
int codegen_ir_to_thumb(CodeGen* cg, const DiskIR* ir, uint32_t count);

#endif // MIMIC_CC_H

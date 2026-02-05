/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC Lexer - Disk-Buffered Tokenization                                 ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Pass 1: source.c → source.tok                                            ║
 * ║  RAM usage: ~2-4KB (I/O buffer + current token)                           ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "mimic.h"
#include "mimic_fat32.h"
#include "mimic_cc.h"

// ============================================================================
// KEYWORD TABLE
// ============================================================================

typedef struct {
    const char* name;
    TokenType   type;
} Keyword;

static const Keyword keywords[] = {
    { "auto",       TOK_AUTO },
    { "break",      TOK_BREAK },
    { "case",       TOK_CASE },
    { "char",       TOK_CHAR_KW },
    { "const",      TOK_CONST },
    { "continue",   TOK_CONTINUE },
    { "default",    TOK_DEFAULT },
    { "do",         TOK_DO },
    { "double",     TOK_DOUBLE },
    { "else",       TOK_ELSE },
    { "enum",       TOK_ENUM },
    { "extern",     TOK_EXTERN },
    { "float",      TOK_FLOAT },
    { "for",        TOK_FOR },
    { "goto",       TOK_GOTO },
    { "if",         TOK_IF },
    { "int",        TOK_INT },
    { "long",       TOK_LONG },
    { "register",   TOK_REGISTER },
    { "return",     TOK_RETURN },
    { "short",      TOK_SHORT },
    { "signed",     TOK_SIGNED },
    { "sizeof",     TOK_SIZEOF },
    { "static",     TOK_STATIC },
    { "struct",     TOK_STRUCT },
    { "switch",     TOK_SWITCH },
    { "typedef",    TOK_TYPEDEF },
    { "union",      TOK_UNION },
    { "unsigned",   TOK_UNSIGNED },
    { "void",       TOK_VOID },
    { "volatile",   TOK_VOLATILE },
    { "while",      TOK_WHILE },
    { NULL,         TOK_EOF }
};

// ============================================================================
// LEXER STATE
// ============================================================================

typedef struct {
    MimicStream*    in;             // Input stream
    MimicStream*    out;            // Output stream (tokens)
    
    int             ch;             // Current character
    uint32_t        line;           // Current line number
    uint32_t        col;            // Current column
    
    // String table (written to separate section of .tok file)
    char*           strings;
    uint32_t        string_size;
    uint32_t        string_cap;
    
    // Statistics
    uint32_t        token_count;
    uint32_t        error_count;
    
    // Error info
    char            error_msg[128];
    uint32_t        error_line;
} LexerState;

// ============================================================================
// CHARACTER HELPERS
// ============================================================================

static int lex_getc(LexerState* lex) {
    int c = mimic_stream_getc(lex->in);
    if (c == '\n') {
        lex->line++;
        lex->col = 0;
    } else if (c >= 0) {
        lex->col++;
    }
    return c;
}

static int lex_peekc(LexerState* lex) {
    int c = mimic_stream_getc(lex->in);
    if (c >= 0) {
        // Simple unget by seeking back
        // (this works because we're using buffered I/O)
        mimic_fseek(lex->in->fd, -1, MIMIC_SEEK_CUR);
        lex->in->buf_pos--;
    }
    return c;
}

static void lex_advance(LexerState* lex) {
    lex->ch = lex_getc(lex);
}

static void lex_error(LexerState* lex, const char* msg) {
    snprintf(lex->error_msg, sizeof(lex->error_msg), 
             "Line %lu: %s", lex->line, msg);
    lex->error_count++;
}

// ============================================================================
// STRING TABLE
// ============================================================================

static uint32_t lex_add_string(LexerState* lex, const char* str, uint32_t len) {
    // Check if we need to grow
    if (lex->string_size + len + 1 > lex->string_cap) {
        // For now, just fail - could implement disk paging here
        lex_error(lex, "String table overflow");
        return 0;
    }
    
    uint32_t offset = lex->string_size;
    memcpy(lex->strings + offset, str, len);
    lex->strings[offset + len] = '\0';
    lex->string_size += len + 1;
    
    return offset;
}

// ============================================================================
// TOKEN OUTPUT
// ============================================================================

static int lex_emit(LexerState* lex, TokenType type, uint32_t value) {
    DiskToken tok;
    tok.type = type;
    tok.flags = 0;
    tok.value = value;
    
    int n = mimic_stream_write(lex->out, &tok, sizeof(tok));
    if (n != sizeof(tok)) {
        lex_error(lex, "Failed to write token");
        return MIMIC_ERR_IO;
    }
    
    lex->token_count++;
    return MIMIC_OK;
}

// ============================================================================
// SKIP WHITESPACE AND COMMENTS
// ============================================================================

static void lex_skip_whitespace(LexerState* lex) {
    while (lex->ch > 0) {
        // Whitespace
        if (lex->ch == ' ' || lex->ch == '\t' || 
            lex->ch == '\n' || lex->ch == '\r') {
            lex_advance(lex);
            continue;
        }
        
        // Line comment
        if (lex->ch == '/' && lex_peekc(lex) == '/') {
            lex_advance(lex);
            lex_advance(lex);
            while (lex->ch > 0 && lex->ch != '\n') {
                lex_advance(lex);
            }
            continue;
        }
        
        // Block comment
        if (lex->ch == '/' && lex_peekc(lex) == '*') {
            lex_advance(lex);
            lex_advance(lex);
            while (lex->ch > 0) {
                if (lex->ch == '*' && lex_peekc(lex) == '/') {
                    lex_advance(lex);
                    lex_advance(lex);
                    break;
                }
                lex_advance(lex);
            }
            continue;
        }
        
        break;
    }
}

// ============================================================================
// TOKEN SCANNING
// ============================================================================

static int lex_scan_number(LexerState* lex) {
    uint32_t value = 0;
    int base = 10;
    
    // Check for hex or octal
    if (lex->ch == '0') {
        lex_advance(lex);
        if (lex->ch == 'x' || lex->ch == 'X') {
            base = 16;
            lex_advance(lex);
        } else if (isdigit(lex->ch)) {
            base = 8;
        } else {
            // Just zero
            return lex_emit(lex, TOK_NUM, 0);
        }
    }
    
    // Scan digits
    while (lex->ch > 0) {
        int digit = -1;
        
        if (lex->ch >= '0' && lex->ch <= '9') {
            digit = lex->ch - '0';
        } else if (base == 16 && lex->ch >= 'a' && lex->ch <= 'f') {
            digit = lex->ch - 'a' + 10;
        } else if (base == 16 && lex->ch >= 'A' && lex->ch <= 'F') {
            digit = lex->ch - 'A' + 10;
        }
        
        if (digit < 0 || digit >= base) break;
        
        value = value * base + digit;
        lex_advance(lex);
    }
    
    // Skip suffix (u, l, ul, etc.)
    while (lex->ch == 'u' || lex->ch == 'U' || 
           lex->ch == 'l' || lex->ch == 'L') {
        lex_advance(lex);
    }
    
    return lex_emit(lex, TOK_NUM, value);
}

static int lex_scan_string(LexerState* lex) {
    char buf[512];
    int len = 0;
    
    lex_advance(lex);  // Skip opening quote
    
    while (lex->ch > 0 && lex->ch != '"' && len < 511) {
        if (lex->ch == '\\') {
            lex_advance(lex);
            switch (lex->ch) {
                case 'n':  buf[len++] = '\n'; break;
                case 't':  buf[len++] = '\t'; break;
                case 'r':  buf[len++] = '\r'; break;
                case '0':  buf[len++] = '\0'; break;
                case '\\': buf[len++] = '\\'; break;
                case '"':  buf[len++] = '"';  break;
                default:   buf[len++] = lex->ch;
            }
        } else {
            buf[len++] = lex->ch;
        }
        lex_advance(lex);
    }
    
    if (lex->ch != '"') {
        lex_error(lex, "Unterminated string");
        return MIMIC_ERR_CORRUPT;
    }
    lex_advance(lex);  // Skip closing quote
    
    buf[len] = '\0';
    uint32_t offset = lex_add_string(lex, buf, len);
    
    return lex_emit(lex, TOK_STR, offset);
}

static int lex_scan_char(LexerState* lex) {
    lex_advance(lex);  // Skip opening quote
    
    uint32_t value = 0;
    
    if (lex->ch == '\\') {
        lex_advance(lex);
        switch (lex->ch) {
            case 'n':  value = '\n'; break;
            case 't':  value = '\t'; break;
            case 'r':  value = '\r'; break;
            case '0':  value = '\0'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            default:   value = lex->ch;
        }
    } else {
        value = lex->ch;
    }
    lex_advance(lex);
    
    if (lex->ch != '\'') {
        lex_error(lex, "Unterminated character literal");
        return MIMIC_ERR_CORRUPT;
    }
    lex_advance(lex);
    
    return lex_emit(lex, TOK_CHAR, value);
}

static int lex_scan_identifier(LexerState* lex) {
    char buf[64];
    int len = 0;
    
    while (lex->ch > 0 && (isalnum(lex->ch) || lex->ch == '_') && len < 63) {
        buf[len++] = lex->ch;
        lex_advance(lex);
    }
    buf[len] = '\0';
    
    // Check if it's a keyword
    for (int i = 0; keywords[i].name; i++) {
        if (strcmp(buf, keywords[i].name) == 0) {
            return lex_emit(lex, keywords[i].type, 0);
        }
    }
    
    // It's an identifier - add to string table
    uint32_t offset = lex_add_string(lex, buf, len);
    return lex_emit(lex, TOK_IDENT, offset);
}

static int lex_scan_preprocessor(LexerState* lex) {
    lex_advance(lex);  // Skip #
    
    // Skip whitespace
    while (lex->ch == ' ' || lex->ch == '\t') {
        lex_advance(lex);
    }
    
    // Read directive name
    char buf[32];
    int len = 0;
    while (isalpha(lex->ch) && len < 31) {
        buf[len++] = lex->ch;
        lex_advance(lex);
    }
    buf[len] = '\0';
    
    TokenType type = TOK_EOF;
    if (strcmp(buf, "include") == 0) type = TOK_PP_INCLUDE;
    else if (strcmp(buf, "define") == 0) type = TOK_PP_DEFINE;
    else if (strcmp(buf, "ifdef") == 0) type = TOK_PP_IFDEF;
    else if (strcmp(buf, "ifndef") == 0) type = TOK_PP_IFNDEF;
    else if (strcmp(buf, "else") == 0) type = TOK_PP_ELSE;
    else if (strcmp(buf, "endif") == 0) type = TOK_PP_ENDIF;
    else if (strcmp(buf, "pragma") == 0) type = TOK_PP_PRAGMA;
    
    if (type == TOK_EOF) {
        lex_error(lex, "Unknown preprocessor directive");
        // Skip to end of line
        while (lex->ch > 0 && lex->ch != '\n') {
            lex_advance(lex);
        }
        return MIMIC_OK;
    }
    
    // For #include, read the filename
    if (type == TOK_PP_INCLUDE) {
        while (lex->ch == ' ' || lex->ch == '\t') {
            lex_advance(lex);
        }
        
        char delim = (lex->ch == '<') ? '>' : '"';
        if (lex->ch == '<' || lex->ch == '"') {
            lex_advance(lex);
            
            char filename[128];
            int flen = 0;
            while (lex->ch > 0 && lex->ch != delim && flen < 127) {
                filename[flen++] = lex->ch;
                lex_advance(lex);
            }
            filename[flen] = '\0';
            
            if (lex->ch == delim) {
                lex_advance(lex);
            }
            
            uint32_t offset = lex_add_string(lex, filename, flen);
            return lex_emit(lex, type, offset);
        }
    }
    
    // For #define, we need special handling
    if (type == TOK_PP_DEFINE) {
        // Skip to end of line for now (simplified)
        while (lex->ch > 0 && lex->ch != '\n') {
            lex_advance(lex);
        }
        return lex_emit(lex, type, 0);
    }
    
    return lex_emit(lex, type, 0);
}

// ============================================================================
// MAIN LEXER FUNCTION
// ============================================================================

static int lex_next_token(LexerState* lex) {
    lex_skip_whitespace(lex);
    
    if (lex->ch < 0) {
        return lex_emit(lex, TOK_EOF, 0);
    }
    
    // Preprocessor
    if (lex->ch == '#') {
        return lex_scan_preprocessor(lex);
    }
    
    // Number
    if (isdigit(lex->ch)) {
        return lex_scan_number(lex);
    }
    
    // String
    if (lex->ch == '"') {
        return lex_scan_string(lex);
    }
    
    // Character
    if (lex->ch == '\'') {
        return lex_scan_char(lex);
    }
    
    // Identifier or keyword
    if (isalpha(lex->ch) || lex->ch == '_') {
        return lex_scan_identifier(lex);
    }
    
    // Operators and punctuation
    int ch = lex->ch;
    lex_advance(lex);
    
    switch (ch) {
        case '+':
            if (lex->ch == '+') { lex_advance(lex); return lex_emit(lex, TOK_INC, 0); }
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_PLUS_ASSIGN, 0); }
            return lex_emit(lex, TOK_PLUS, 0);
            
        case '-':
            if (lex->ch == '-') { lex_advance(lex); return lex_emit(lex, TOK_DEC, 0); }
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_MINUS_ASSIGN, 0); }
            if (lex->ch == '>') { lex_advance(lex); return lex_emit(lex, TOK_ARROW, 0); }
            return lex_emit(lex, TOK_MINUS, 0);
            
        case '*':
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_STAR_ASSIGN, 0); }
            return lex_emit(lex, TOK_STAR, 0);
            
        case '/':
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_SLASH_ASSIGN, 0); }
            return lex_emit(lex, TOK_SLASH, 0);
            
        case '%':
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_PERCENT_ASSIGN, 0); }
            return lex_emit(lex, TOK_PERCENT, 0);
            
        case '&':
            if (lex->ch == '&') { lex_advance(lex); return lex_emit(lex, TOK_AND, 0); }
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_AMP_ASSIGN, 0); }
            return lex_emit(lex, TOK_AMP, 0);
            
        case '|':
            if (lex->ch == '|') { lex_advance(lex); return lex_emit(lex, TOK_OR, 0); }
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_PIPE_ASSIGN, 0); }
            return lex_emit(lex, TOK_PIPE, 0);
            
        case '^':
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_CARET_ASSIGN, 0); }
            return lex_emit(lex, TOK_CARET, 0);
            
        case '~':
            return lex_emit(lex, TOK_TILDE, 0);
            
        case '!':
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_NE, 0); }
            return lex_emit(lex, TOK_BANG, 0);
            
        case '=':
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_EQ, 0); }
            return lex_emit(lex, TOK_ASSIGN, 0);
            
        case '<':
            if (lex->ch == '<') {
                lex_advance(lex);
                if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_SHL_ASSIGN, 0); }
                return lex_emit(lex, TOK_SHL, 0);
            }
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_LE, 0); }
            return lex_emit(lex, TOK_LT, 0);
            
        case '>':
            if (lex->ch == '>') {
                lex_advance(lex);
                if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_SHR_ASSIGN, 0); }
                return lex_emit(lex, TOK_SHR, 0);
            }
            if (lex->ch == '=') { lex_advance(lex); return lex_emit(lex, TOK_GE, 0); }
            return lex_emit(lex, TOK_GT, 0);
            
        case '?': return lex_emit(lex, TOK_QUESTION, 0);
        case ':': return lex_emit(lex, TOK_COLON, 0);
        case ';': return lex_emit(lex, TOK_SEMICOLON, 0);
        case ',': return lex_emit(lex, TOK_COMMA, 0);
        case '.': return lex_emit(lex, TOK_DOT, 0);
        case '(': return lex_emit(lex, TOK_LPAREN, 0);
        case ')': return lex_emit(lex, TOK_RPAREN, 0);
        case '[': return lex_emit(lex, TOK_LBRACKET, 0);
        case ']': return lex_emit(lex, TOK_RBRACKET, 0);
        case '{': return lex_emit(lex, TOK_LBRACE, 0);
        case '}': return lex_emit(lex, TOK_RBRACE, 0);
        
        default:
            lex_error(lex, "Unexpected character");
            return MIMIC_ERR_CORRUPT;
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

int mimic_cc_lex(CompilerState* cc, const char* source, const char* tok_output) {
    // Allocate I/O buffers
    uint8_t* in_buf = mimic_kmalloc(MIMIC_CC_IO_BUFFER);
    uint8_t* out_buf = mimic_kmalloc(MIMIC_CC_IO_BUFFER);
    char* string_buf = mimic_kmalloc(8192);  // 8KB string table
    
    if (!in_buf || !out_buf || !string_buf) {
        if (in_buf) mimic_kfree(in_buf);
        if (out_buf) mimic_kfree(out_buf);
        if (string_buf) mimic_kfree(string_buf);
        return MIMIC_ERR_NOMEM;
    }
    
    // Initialize lexer state
    LexerState lex = {0};
    lex.line = 1;
    lex.col = 0;
    lex.strings = string_buf;
    lex.string_size = 0;
    lex.string_cap = 8192;
    
    // Open input stream
    MimicStream in_stream, out_stream;
    int err = mimic_stream_open(&in_stream, source, MIMIC_FILE_READ, in_buf, MIMIC_CC_IO_BUFFER);
    if (err != MIMIC_OK) {
        mimic_kfree(in_buf);
        mimic_kfree(out_buf);
        mimic_kfree(string_buf);
        return err;
    }
    
    // Open output stream
    err = mimic_stream_open(&out_stream, tok_output, 
                            MIMIC_FILE_WRITE | MIMIC_FILE_CREATE | MIMIC_FILE_TRUNC,
                            out_buf, MIMIC_CC_IO_BUFFER);
    if (err != MIMIC_OK) {
        mimic_stream_close(&in_stream);
        mimic_kfree(in_buf);
        mimic_kfree(out_buf);
        mimic_kfree(string_buf);
        return err;
    }
    
    lex.in = &in_stream;
    lex.out = &out_stream;
    
    // Write placeholder header (will update at end)
    uint32_t header[4] = {0};  // [token_count, string_offset, string_size, reserved]
    mimic_stream_write(&out_stream, header, sizeof(header));
    
    // Read first character
    lex_advance(&lex);
    
    // Tokenize
    while (lex.ch >= 0) {
        err = lex_next_token(&lex);
        if (err != MIMIC_OK && err != MIMIC_ERR_CORRUPT) {
            break;
        }
    }
    
    // Write EOF token
    lex_emit(&lex, TOK_EOF, 0);
    
    // Flush token output
    mimic_stream_flush(&out_stream);
    
    // Get current position (where strings will start)
    uint32_t string_offset = mimic_ftell(out_stream.fd);
    
    // Write string table
    mimic_fwrite(out_stream.fd, lex.strings, lex.string_size);
    
    // Update header
    header[0] = lex.token_count;
    header[1] = string_offset;
    header[2] = lex.string_size;
    header[3] = 0;
    
    mimic_fseek(out_stream.fd, 0, MIMIC_SEEK_SET);
    mimic_fwrite(out_stream.fd, header, sizeof(header));
    
    // Cleanup
    mimic_stream_close(&in_stream);
    mimic_stream_close(&out_stream);
    mimic_kfree(in_buf);
    mimic_kfree(out_buf);
    mimic_kfree(string_buf);
    
    // Update compiler state
    cc->tokens_processed = lex.token_count;
    if (lex.error_count > 0) {
        cc->error_count += lex.error_count;
        strncpy(cc->error_msg, lex.error_msg, sizeof(cc->error_msg) - 1);
        cc->error_line = lex.error_line;
        return MIMIC_ERR_CORRUPT;
    }
    
    if (cc->verbose) {
        printf("[LEX] %lu tokens, %lu bytes strings\n", 
               lex.token_count, lex.string_size);
    }
    
    return MIMIC_OK;
}

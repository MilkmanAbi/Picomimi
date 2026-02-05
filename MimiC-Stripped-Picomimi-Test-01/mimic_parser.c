/**
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║  MimiC Parser - Disk-Buffered Parsing                                     ║
 * ╠═══════════════════════════════════════════════════════════════════════════╣
 * ║  Pass 2: source.tok → source.ast                                          ║
 * ║  RAM usage: ~8-16KB (parse stack + current scope symbols)                 ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include <string.h>
#include <stdio.h>

#include "mimic.h"
#include "mimic_fat32.h"
#include "mimic_cc.h"

// ============================================================================
// PARSER STATE
// ============================================================================

typedef struct {
    MimicStream*    in;             // Token input
    MimicStream*    out;            // AST output
    CompilerState*  cc;
    
    // Token stream
    DiskToken       current;
    DiskToken       peek;
    bool            has_peek;
    
    // String table (loaded from .tok file)
    char*           strings;
    uint32_t        string_size;
    
    // AST node output
    uint32_t        node_count;
    uint32_t        node_offset;    // Current write position
    
    // Error handling
    uint32_t        error_count;
    char            error_msg[128];
} ParserState;

// ============================================================================
// TOKEN STREAM
// ============================================================================

static int parser_read_token(ParserState* p, DiskToken* tok) {
    int n = mimic_stream_read(p->in, tok, sizeof(DiskToken));
    if (n != sizeof(DiskToken)) {
        tok->type = TOK_EOF;
        tok->value = 0;
        return MIMIC_ERR_IO;
    }
    return MIMIC_OK;
}

static void parser_advance(ParserState* p) {
    if (p->has_peek) {
        p->current = p->peek;
        p->has_peek = false;
    } else {
        parser_read_token(p, &p->current);
    }
}

static DiskToken* parser_peek(ParserState* p) {
    if (!p->has_peek) {
        parser_read_token(p, &p->peek);
        p->has_peek = true;
    }
    return &p->peek;
}

static bool parser_check(ParserState* p, TokenType type) {
    return p->current.type == type;
}

static bool parser_match(ParserState* p, TokenType type) {
    if (parser_check(p, type)) {
        parser_advance(p);
        return true;
    }
    return false;
}

static void parser_error(ParserState* p, const char* msg) {
    snprintf(p->error_msg, sizeof(p->error_msg), "%s (token type %d)", 
             msg, p->current.type);
    p->error_count++;
}

static bool parser_expect(ParserState* p, TokenType type, const char* msg) {
    if (!parser_match(p, type)) {
        parser_error(p, msg);
        return false;
    }
    return true;
}

// Get string from string table
static const char* parser_get_string(ParserState* p, uint32_t offset) {
    if (offset >= p->string_size) return "";
    return p->strings + offset;
}

// ============================================================================
// AST OUTPUT
// ============================================================================

// Write an AST node to disk, returns offset
static uint32_t ast_emit_node(ParserState* p, ASTNodeType type, uint8_t flags,
                               uint32_t data, uint32_t* children, uint16_t child_count) {
    uint32_t offset = p->node_offset;
    
    DiskASTNode node;
    node.type = type;
    node.flags = flags;
    node.child_count = child_count;
    node.data = data;
    
    mimic_stream_write(p->out, &node, sizeof(node));
    p->node_offset += sizeof(node);
    
    // Write child offsets
    if (child_count > 0 && children) {
        mimic_stream_write(p->out, children, child_count * sizeof(uint32_t));
        p->node_offset += child_count * sizeof(uint32_t);
    }
    
    p->node_count++;
    return offset;
}

// Emit a leaf node (no children)
static uint32_t ast_leaf(ParserState* p, ASTNodeType type, uint32_t data) {
    return ast_emit_node(p, type, 0, data, NULL, 0);
}

// ============================================================================
// EXPRESSION PARSING (Pratt parser / precedence climbing)
// ============================================================================

// Operator precedence
static int get_precedence(TokenType op) {
    switch (op) {
        case TOK_COMMA:         return 1;
        case TOK_ASSIGN:
        case TOK_PLUS_ASSIGN:
        case TOK_MINUS_ASSIGN:
        case TOK_STAR_ASSIGN:
        case TOK_SLASH_ASSIGN:
        case TOK_PERCENT_ASSIGN:
        case TOK_AMP_ASSIGN:
        case TOK_PIPE_ASSIGN:
        case TOK_CARET_ASSIGN:
        case TOK_SHL_ASSIGN:
        case TOK_SHR_ASSIGN:    return 2;
        case TOK_QUESTION:      return 3;
        case TOK_OR:            return 4;
        case TOK_AND:           return 5;
        case TOK_PIPE:          return 6;
        case TOK_CARET:         return 7;
        case TOK_AMP:           return 8;
        case TOK_EQ:
        case TOK_NE:            return 9;
        case TOK_LT:
        case TOK_GT:
        case TOK_LE:
        case TOK_GE:            return 10;
        case TOK_SHL:
        case TOK_SHR:           return 11;
        case TOK_PLUS:
        case TOK_MINUS:         return 12;
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT:       return 13;
        default:                return 0;
    }
}

static uint32_t parse_expression(ParserState* p, int min_prec);
static uint32_t parse_unary(ParserState* p);

static uint32_t parse_primary(ParserState* p) {
    // Number
    if (parser_check(p, TOK_NUM)) {
        uint32_t val = p->current.value;
        parser_advance(p);
        return ast_leaf(p, AST_NUM, val);
    }
    
    // String
    if (parser_check(p, TOK_STR)) {
        uint32_t offset = p->current.value;
        parser_advance(p);
        return ast_leaf(p, AST_STR, offset);
    }
    
    // Character
    if (parser_check(p, TOK_CHAR)) {
        uint32_t val = p->current.value;
        parser_advance(p);
        return ast_leaf(p, AST_NUM, val);
    }
    
    // Identifier
    if (parser_check(p, TOK_IDENT)) {
        uint32_t offset = p->current.value;
        parser_advance(p);
        return ast_leaf(p, AST_IDENT, offset);
    }
    
    // Parenthesized expression
    if (parser_match(p, TOK_LPAREN)) {
        uint32_t expr = parse_expression(p, 1);
        parser_expect(p, TOK_RPAREN, "Expected ')'");
        return expr;
    }
    
    // sizeof
    if (parser_match(p, TOK_SIZEOF)) {
        parser_expect(p, TOK_LPAREN, "Expected '(' after sizeof");
        uint32_t expr = parse_unary(p);  // Could be type or expression
        parser_expect(p, TOK_RPAREN, "Expected ')'");
        return ast_emit_node(p, AST_SIZEOF, 0, 0, &expr, 1);
    }
    
    parser_error(p, "Expected expression");
    return 0;
}

static uint32_t parse_postfix(ParserState* p) {
    uint32_t expr = parse_primary(p);
    
    while (true) {
        // Function call
        if (parser_match(p, TOK_LPAREN)) {
            uint32_t args[16];
            uint16_t argc = 0;
            
            if (!parser_check(p, TOK_RPAREN)) {
                do {
                    if (argc >= 16) {
                        parser_error(p, "Too many arguments");
                        break;
                    }
                    args[argc++] = parse_expression(p, 2);  // Above comma precedence
                } while (parser_match(p, TOK_COMMA));
            }
            
            parser_expect(p, TOK_RPAREN, "Expected ')'");
            
            // Create call node with function + args as children
            uint32_t children[17];
            children[0] = expr;
            for (int i = 0; i < argc; i++) {
                children[i + 1] = args[i];
            }
            expr = ast_emit_node(p, AST_CALL, 0, argc, children, argc + 1);
            continue;
        }
        
        // Array subscript
        if (parser_match(p, TOK_LBRACKET)) {
            uint32_t index = parse_expression(p, 1);
            parser_expect(p, TOK_RBRACKET, "Expected ']'");
            
            uint32_t children[2] = {expr, index};
            expr = ast_emit_node(p, AST_INDEX, 0, 0, children, 2);
            continue;
        }
        
        // Member access
        if (parser_match(p, TOK_DOT)) {
            if (!parser_check(p, TOK_IDENT)) {
                parser_error(p, "Expected member name");
                break;
            }
            uint32_t member = p->current.value;
            parser_advance(p);
            
            expr = ast_emit_node(p, AST_MEMBER, 0, member, &expr, 1);
            continue;
        }
        
        // Arrow access
        if (parser_match(p, TOK_ARROW)) {
            if (!parser_check(p, TOK_IDENT)) {
                parser_error(p, "Expected member name");
                break;
            }
            uint32_t member = p->current.value;
            parser_advance(p);
            
            expr = ast_emit_node(p, AST_MEMBER, 1, member, &expr, 1);  // flag=1 for arrow
            continue;
        }
        
        // Postfix increment/decrement
        if (parser_match(p, TOK_INC)) {
            expr = ast_emit_node(p, AST_UNOP, 1, TOK_INC, &expr, 1);  // flag=1 for postfix
            continue;
        }
        if (parser_match(p, TOK_DEC)) {
            expr = ast_emit_node(p, AST_UNOP, 1, TOK_DEC, &expr, 1);
            continue;
        }
        
        break;
    }
    
    return expr;
}

static uint32_t parse_unary(ParserState* p) {
    // Prefix operators
    if (parser_match(p, TOK_MINUS)) {
        uint32_t expr = parse_unary(p);
        return ast_emit_node(p, AST_UNOP, 0, TOK_MINUS, &expr, 1);
    }
    if (parser_match(p, TOK_PLUS)) {
        return parse_unary(p);  // Unary + is a no-op
    }
    if (parser_match(p, TOK_BANG)) {
        uint32_t expr = parse_unary(p);
        return ast_emit_node(p, AST_UNOP, 0, TOK_BANG, &expr, 1);
    }
    if (parser_match(p, TOK_TILDE)) {
        uint32_t expr = parse_unary(p);
        return ast_emit_node(p, AST_UNOP, 0, TOK_TILDE, &expr, 1);
    }
    if (parser_match(p, TOK_STAR)) {
        uint32_t expr = parse_unary(p);
        return ast_emit_node(p, AST_UNOP, 0, TOK_STAR, &expr, 1);  // Dereference
    }
    if (parser_match(p, TOK_AMP)) {
        uint32_t expr = parse_unary(p);
        return ast_emit_node(p, AST_UNOP, 0, TOK_AMP, &expr, 1);  // Address-of
    }
    if (parser_match(p, TOK_INC)) {
        uint32_t expr = parse_unary(p);
        return ast_emit_node(p, AST_UNOP, 0, TOK_INC, &expr, 1);  // Prefix ++
    }
    if (parser_match(p, TOK_DEC)) {
        uint32_t expr = parse_unary(p);
        return ast_emit_node(p, AST_UNOP, 0, TOK_DEC, &expr, 1);  // Prefix --
    }
    
    // TODO: Cast expressions
    
    return parse_postfix(p);
}

static uint32_t parse_expression(ParserState* p, int min_prec) {
    uint32_t left = parse_unary(p);
    
    while (true) {
        int prec = get_precedence(p->current.type);
        if (prec < min_prec) break;
        
        TokenType op = p->current.type;
        parser_advance(p);
        
        // Ternary operator
        if (op == TOK_QUESTION) {
            uint32_t then_expr = parse_expression(p, 1);
            parser_expect(p, TOK_COLON, "Expected ':' in ternary");
            uint32_t else_expr = parse_expression(p, prec);
            
            uint32_t children[3] = {left, then_expr, else_expr};
            left = ast_emit_node(p, AST_COND, 0, 0, children, 3);
            continue;
        }
        
        // Assignment operators are right-associative
        int next_prec = (op >= TOK_ASSIGN && op <= TOK_SHR_ASSIGN) ? prec : prec + 1;
        uint32_t right = parse_expression(p, next_prec);
        
        // Create binary operation node
        uint32_t children[2] = {left, right};
        ASTNodeType node_type = (op >= TOK_ASSIGN && op <= TOK_SHR_ASSIGN) 
                                 ? AST_ASSIGN : AST_BINOP;
        left = ast_emit_node(p, node_type, 0, op, children, 2);
    }
    
    return left;
}

// ============================================================================
// STATEMENT PARSING
// ============================================================================

static uint32_t parse_statement(ParserState* p);
static uint32_t parse_block(ParserState* p);

static uint32_t parse_if_statement(ParserState* p) {
    parser_expect(p, TOK_LPAREN, "Expected '(' after 'if'");
    uint32_t cond = parse_expression(p, 1);
    parser_expect(p, TOK_RPAREN, "Expected ')'");
    
    uint32_t then_stmt = parse_statement(p);
    
    uint32_t children[3] = {cond, then_stmt, 0};
    uint16_t count = 2;
    
    if (parser_match(p, TOK_ELSE)) {
        children[2] = parse_statement(p);
        count = 3;
    }
    
    return ast_emit_node(p, AST_IF, 0, 0, children, count);
}

static uint32_t parse_while_statement(ParserState* p) {
    parser_expect(p, TOK_LPAREN, "Expected '(' after 'while'");
    uint32_t cond = parse_expression(p, 1);
    parser_expect(p, TOK_RPAREN, "Expected ')'");
    
    uint32_t body = parse_statement(p);
    
    uint32_t children[2] = {cond, body};
    return ast_emit_node(p, AST_WHILE, 0, 0, children, 2);
}

static uint32_t parse_for_statement(ParserState* p) {
    parser_expect(p, TOK_LPAREN, "Expected '(' after 'for'");
    
    // Init
    uint32_t init = 0;
    if (!parser_check(p, TOK_SEMICOLON)) {
        init = parse_expression(p, 1);
    }
    parser_expect(p, TOK_SEMICOLON, "Expected ';'");
    
    // Condition
    uint32_t cond = 0;
    if (!parser_check(p, TOK_SEMICOLON)) {
        cond = parse_expression(p, 1);
    }
    parser_expect(p, TOK_SEMICOLON, "Expected ';'");
    
    // Update
    uint32_t update = 0;
    if (!parser_check(p, TOK_RPAREN)) {
        update = parse_expression(p, 1);
    }
    parser_expect(p, TOK_RPAREN, "Expected ')'");
    
    uint32_t body = parse_statement(p);
    
    uint32_t children[4] = {init, cond, update, body};
    return ast_emit_node(p, AST_FOR, 0, 0, children, 4);
}

static uint32_t parse_return_statement(ParserState* p) {
    uint32_t expr = 0;
    if (!parser_check(p, TOK_SEMICOLON)) {
        expr = parse_expression(p, 1);
    }
    parser_expect(p, TOK_SEMICOLON, "Expected ';'");
    
    if (expr) {
        return ast_emit_node(p, AST_RETURN, 0, 0, &expr, 1);
    }
    return ast_emit_node(p, AST_RETURN, 0, 0, NULL, 0);
}

static uint32_t parse_statement(ParserState* p) {
    // Block
    if (parser_check(p, TOK_LBRACE)) {
        return parse_block(p);
    }
    
    // If
    if (parser_match(p, TOK_IF)) {
        return parse_if_statement(p);
    }
    
    // While
    if (parser_match(p, TOK_WHILE)) {
        return parse_while_statement(p);
    }
    
    // For
    if (parser_match(p, TOK_FOR)) {
        return parse_for_statement(p);
    }
    
    // Return
    if (parser_match(p, TOK_RETURN)) {
        return parse_return_statement(p);
    }
    
    // Break
    if (parser_match(p, TOK_BREAK)) {
        parser_expect(p, TOK_SEMICOLON, "Expected ';'");
        return ast_emit_node(p, AST_BREAK, 0, 0, NULL, 0);
    }
    
    // Continue
    if (parser_match(p, TOK_CONTINUE)) {
        parser_expect(p, TOK_SEMICOLON, "Expected ';'");
        return ast_emit_node(p, AST_CONTINUE, 0, 0, NULL, 0);
    }
    
    // Empty statement
    if (parser_match(p, TOK_SEMICOLON)) {
        return ast_emit_node(p, AST_EXPR_STMT, 0, 0, NULL, 0);
    }
    
    // Expression statement
    uint32_t expr = parse_expression(p, 1);
    parser_expect(p, TOK_SEMICOLON, "Expected ';'");
    return ast_emit_node(p, AST_EXPR_STMT, 0, 0, &expr, 1);
}

static uint32_t parse_block(ParserState* p) {
    parser_expect(p, TOK_LBRACE, "Expected '{'");
    
    uint32_t stmts[128];  // Max statements per block
    uint16_t count = 0;
    
    while (!parser_check(p, TOK_RBRACE) && !parser_check(p, TOK_EOF)) {
        if (count >= 128) {
            parser_error(p, "Too many statements in block");
            break;
        }
        stmts[count++] = parse_statement(p);
    }
    
    parser_expect(p, TOK_RBRACE, "Expected '}'");
    
    return ast_emit_node(p, AST_BLOCK, 0, 0, stmts, count);
}

// ============================================================================
// DECLARATION PARSING
// ============================================================================

// Check if current token starts a type
static bool is_type_start(ParserState* p) {
    switch (p->current.type) {
        case TOK_VOID:
        case TOK_CHAR_KW:
        case TOK_SHORT:
        case TOK_INT:
        case TOK_LONG:
        case TOK_FLOAT:
        case TOK_DOUBLE:
        case TOK_SIGNED:
        case TOK_UNSIGNED:
        case TOK_STRUCT:
        case TOK_UNION:
        case TOK_ENUM:
        case TOK_CONST:
        case TOK_VOLATILE:
        case TOK_STATIC:
        case TOK_EXTERN:
        case TOK_TYPEDEF:
            return true;
        default:
            return false;
    }
}

static uint32_t parse_type_spec(ParserState* p) {
    // Simple type parsing - just capture the tokens
    uint32_t type_flags = 0;
    
    while (is_type_start(p)) {
        type_flags |= (1 << p->current.type);
        parser_advance(p);
    }
    
    return type_flags;
}

static uint32_t parse_declaration(ParserState* p) {
    uint32_t type_spec = parse_type_spec(p);
    
    // Get declarator name
    uint32_t name_offset = 0;
    int ptr_depth = 0;
    
    // Count pointer asterisks
    while (parser_match(p, TOK_STAR)) {
        ptr_depth++;
    }
    
    if (parser_check(p, TOK_IDENT)) {
        name_offset = p->current.value;
        parser_advance(p);
    } else {
        parser_error(p, "Expected identifier");
        return 0;
    }
    
    // Function declaration/definition
    if (parser_check(p, TOK_LPAREN)) {
        parser_advance(p);
        
        uint32_t params[16];
        uint16_t param_count = 0;
        
        if (!parser_check(p, TOK_RPAREN)) {
            do {
                uint32_t ptype = parse_type_spec(p);
                uint32_t pname = 0;
                
                while (parser_match(p, TOK_STAR)) {}  // Skip pointer stars
                
                if (parser_check(p, TOK_IDENT)) {
                    pname = p->current.value;
                    parser_advance(p);
                }
                
                params[param_count++] = ast_emit_node(p, AST_PARAM, 0, pname, &ptype, 1);
            } while (parser_match(p, TOK_COMMA));
        }
        
        parser_expect(p, TOK_RPAREN, "Expected ')'");
        
        // Function definition
        if (parser_check(p, TOK_LBRACE)) {
            uint32_t body = parse_block(p);
            
            uint32_t children[18];  // type + name + params + body
            children[0] = type_spec;
            children[1] = ptr_depth;
            for (int i = 0; i < param_count; i++) {
                children[i + 2] = params[i];
            }
            children[param_count + 2] = body;
            
            return ast_emit_node(p, AST_FUNC_DEF, 0, name_offset, children, param_count + 3);
        }
        
        // Function declaration
        parser_expect(p, TOK_SEMICOLON, "Expected ';'");
        
        uint32_t children[16];
        children[0] = type_spec;
        for (int i = 0; i < param_count; i++) {
            children[i + 1] = params[i];
        }
        return ast_emit_node(p, AST_FUNC_DECL, 0, name_offset, children, param_count + 1);
    }
    
    // Variable declaration
    // TODO: Handle arrays, initializers
    parser_expect(p, TOK_SEMICOLON, "Expected ';'");
    
    uint32_t children[2] = {type_spec, (uint32_t)ptr_depth};
    return ast_emit_node(p, AST_VAR_DECL, 0, name_offset, children, 2);
}

// ============================================================================
// TOP-LEVEL PARSING
// ============================================================================

static uint32_t parse_translation_unit(ParserState* p) {
    uint32_t decls[256];  // Max top-level declarations
    uint16_t count = 0;
    
    while (!parser_check(p, TOK_EOF) && count < 256) {
        // Skip preprocessor tokens for now
        if (p->current.type >= TOK_PP_DEFINE && p->current.type <= TOK_PP_PRAGMA) {
            parser_advance(p);
            continue;
        }
        
        decls[count++] = parse_declaration(p);
        
        if (p->error_count > 10) {
            parser_error(p, "Too many errors, aborting");
            break;
        }
    }
    
    return ast_emit_node(p, AST_PROGRAM, 0, 0, decls, count);
}

// ============================================================================
// PUBLIC API
// ============================================================================

int mimic_cc_parse(CompilerState* cc, const char* tok_input, const char* ast_output) {
    // Allocate buffers
    uint8_t* in_buf = mimic_kmalloc(MIMIC_CC_IO_BUFFER);
    uint8_t* out_buf = mimic_kmalloc(MIMIC_CC_IO_BUFFER);
    
    if (!in_buf || !out_buf) {
        if (in_buf) mimic_kfree(in_buf);
        if (out_buf) mimic_kfree(out_buf);
        return MIMIC_ERR_NOMEM;
    }
    
    // Open input token file
    int fd = mimic_fopen(tok_input, MIMIC_FILE_READ);
    if (fd < 0) {
        mimic_kfree(in_buf);
        mimic_kfree(out_buf);
        return fd;
    }
    
    // Read header
    uint32_t header[4];
    mimic_fread(fd, header, sizeof(header));
    
    uint32_t token_count = header[0];
    uint32_t string_offset = header[1];
    uint32_t string_size = header[2];
    
    // Load string table
    char* strings = mimic_kmalloc(string_size + 1);
    if (!strings) {
        mimic_fclose(fd);
        mimic_kfree(in_buf);
        mimic_kfree(out_buf);
        return MIMIC_ERR_NOMEM;
    }
    
    mimic_fseek(fd, string_offset, MIMIC_SEEK_SET);
    mimic_fread(fd, strings, string_size);
    strings[string_size] = '\0';
    
    // Rewind to token start
    mimic_fseek(fd, sizeof(header), MIMIC_SEEK_SET);
    
    // Open as stream
    MimicStream in_stream;
    in_stream.fd = fd;
    in_stream.buffer = in_buf;
    in_stream.buf_size = MIMIC_CC_IO_BUFFER;
    in_stream.buf_pos = 0;
    in_stream.buf_len = 0;
    in_stream.eof = false;
    
    // Open output stream
    MimicStream out_stream;
    int err = mimic_stream_open(&out_stream, ast_output,
                                 MIMIC_FILE_WRITE | MIMIC_FILE_CREATE | MIMIC_FILE_TRUNC,
                                 out_buf, MIMIC_CC_IO_BUFFER);
    if (err != MIMIC_OK) {
        mimic_fclose(fd);
        mimic_kfree(in_buf);
        mimic_kfree(out_buf);
        mimic_kfree(strings);
        return err;
    }
    
    // Initialize parser
    ParserState p = {0};
    p.in = &in_stream;
    p.out = &out_stream;
    p.cc = cc;
    p.strings = strings;
    p.string_size = string_size;
    p.has_peek = false;
    
    // Write placeholder header
    uint32_t ast_header[4] = {0};  // [node_count, string_offset, string_size, root_offset]
    mimic_stream_write(&out_stream, ast_header, sizeof(ast_header));
    p.node_offset = sizeof(ast_header);
    
    // Read first token
    parser_advance(&p);
    
    // Parse
    uint32_t root = parse_translation_unit(&p);
    
    // Flush output
    mimic_stream_flush(&out_stream);
    
    // Copy string table to AST file
    uint32_t ast_string_offset = mimic_ftell(out_stream.fd);
    mimic_fwrite(out_stream.fd, strings, string_size);
    
    // Update header
    ast_header[0] = p.node_count;
    ast_header[1] = ast_string_offset;
    ast_header[2] = string_size;
    ast_header[3] = root;
    
    mimic_fseek(out_stream.fd, 0, MIMIC_SEEK_SET);
    mimic_fwrite(out_stream.fd, ast_header, sizeof(ast_header));
    
    // Cleanup
    mimic_fclose(fd);
    mimic_stream_close(&out_stream);
    mimic_kfree(in_buf);
    mimic_kfree(out_buf);
    mimic_kfree(strings);
    
    // Update compiler state
    cc->nodes_created = p.node_count;
    if (p.error_count > 0) {
        cc->error_count += p.error_count;
        strncpy(cc->error_msg, p.error_msg, sizeof(cc->error_msg) - 1);
        return MIMIC_ERR_CORRUPT;
    }
    
    if (cc->verbose) {
        printf("[PARSE] %lu AST nodes\n", p.node_count);
    }
    
    return MIMIC_OK;
}

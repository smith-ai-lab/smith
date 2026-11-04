// The external scanner of the .smith grammar: the layout tokens of the syntax of #1428 (the same rules as `layout` in
// front/lex.smith, #1808). A `:` that ends a line and is followed by a more indented line opens a block (`_indent`),
// a statement ends with `_newline`, and every level a line drops closes with `_dedent`. The tokens have no width: the
// scanner marks the end of the token where it started and only looks ahead, so the comments and blank lines in between
// are read by the grammar as usual. A token is returned only where the grammar expects it, so a line break inside
// `(` or `[` (where no rule takes a layout token) counts for nothing, and C and the old forms never see them.
#include <stdint.h>
#include <string.h>

#include "tree_sitter/parser.h"

enum { Token_Indent, Token_Dedent, Token_Newline };

enum { Limit_Levels = 128, Limit_TabColumns = 4 };

typedef struct {
    uint16_t columns[Limit_Levels]; // the indentation of each open block
    uint8_t depth;
} Scanner;

void *tree_sitter_smith_external_scanner_create(void) {
    Scanner *s = (Scanner *)__builtin_calloc(1, sizeof(Scanner));
    return s;
}

void tree_sitter_smith_external_scanner_destroy(void *payload) {
    __builtin_free(payload);
}

unsigned tree_sitter_smith_external_scanner_serialize(void *payload, char *buffer) {
    const Scanner *s = (const Scanner *)payload;
    unsigned size = (unsigned)(s->depth * sizeof(uint16_t));
    buffer[0] = (char)s->depth;
    memcpy(buffer + 1, s->columns, size);
    return size + 1;
}

void tree_sitter_smith_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *s = (Scanner *)payload;
    s->depth = 0;
    if (length == 0) return;
    s->depth = (uint8_t)buffer[0];
    if (s->depth > Limit_Levels || length < 1u + s->depth * sizeof(uint16_t)) {
        s->depth = 0;
        return;
    }
    memcpy(s->columns, buffer + 1, s->depth * sizeof(uint16_t));
}

bool tree_sitter_smith_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid) {
    Scanner *s = (Scanner *)payload;
    // Error recovery marks every token valid, and the grammar never allows all three at once.
    if (valid[Token_Indent] && valid[Token_Dedent] && valid[Token_Newline]) return false;
    lexer->mark_end(lexer);
    bool found_line_end = false;
    unsigned column = 0;
    for (uint32_t scanned = 0; scanned < UINT32_MAX; scanned++) {
        int32_t c = lexer->lookahead;
        if (c == '\n') {
            found_line_end = true;
            column = 0;
        } else if (c == ' ') {
            column += 1;
        } else if (c == '\t') {
            column += Limit_TabColumns;
        } else if (c == '\r') {
            column = 0;
        } else if (c == '#') {
            // A comment after code on the line is the grammar's: let it read it, the scanner is asked again after.
            if (!found_line_end) return false;
            while (lexer->lookahead != 0 && lexer->lookahead != '\n')
                lexer->advance(lexer, false);
            continue;
        } else if (c == 0) {
            found_line_end = true;
            column = 0;
            break;
        } else {
            break;
        }
        lexer->advance(lexer, false);
    }
    if (!found_line_end) return false;
    unsigned current = s->depth == 0 ? 0 : s->columns[s->depth - 1];
    if (valid[Token_Indent] && column > current && s->depth < Limit_Levels) {
        s->columns[s->depth++] = (uint16_t)column;
        lexer->result_symbol = Token_Indent;
        return true;
    }
    if (valid[Token_Newline]) {
        lexer->result_symbol = Token_Newline;
        return true;
    }
    if (valid[Token_Dedent] && column < current) {
        s->depth -= 1;
        lexer->result_symbol = Token_Dedent;
        return true;
    }
    return false;
}

// The lowering of .smith files: C with `module`, `import` and `export`, and sized type names, turned into C. The
// language is described in knowledge/smith-language.md; this file is the state machine, written against runtime/lang.h
// so that it knows nothing of clang. It is linked into smith.
//
// It is given the tokens of one file in order, after macro expansion, and says what replaces each. Its work:
//   - `module a.b;` and `import ...;` lines are consumed. Imports bind a name (`rp.open` becomes `game_replay_open`),
//     and `import <h>;` includes the header, whose names are then reached as `c.name`.
//   - In a module, a file-scope declaration is read whole (its head: everything up to the `;`, `=` or `{`), because
//     what it needs depends on all of it: a prefix on the names it declares (`game_replay_open`), `static` on a private
//     function or variable, a prototype with the `::` symbol (an `__asm__` label) for an exported function.
//   - Sized type names become the standard types.
//   - Every exported declaration is also written, as C, to the module's generated header.
// Nothing here allocates: the tables are fixed, and when one fills the file is passed through unchanged after an error.

#include "core.h"
#include "lang_main.h"
#include "lang_out.h"
#include "lang_slice.h"

#include "lang.h"

char smith_lang_chars[smith_lang_chars_max];
char smith_lang_scratch[smith_lang_scratch_max];
char smith_lang_header_body[smith_lang_header_max];
char smith_lang_header_full[smith_lang_header_max + 4096];
char smith_lang_dump[smith_lang_dump_max];
SmithLang smith_lang_instance;

SmithLangCounter smith_lang_counter = {.slice_cut = smith_lang_counter_cut_start,
                                       .slice_convert = smith_lang_counter_convert_start};

typedef struct {
    const char *text;
    size_t length;
    SmithLangWord word;
} SmithLangWordEntry;

// An entry of the keyword table, with its length known at compile time.
#define SMITH_LANG_WORD(text, word) {text, sizeof(text) - 1, word}

static const SmithLangWordEntry smith_lang_words[] = {
    SMITH_LANG_WORD("_Alignas", SmithLangWord_Keyword),
    SMITH_LANG_WORD("_Alignof", SmithLangWord_Keyword),
    SMITH_LANG_WORD("_Atomic", SmithLangWord_Qualifier),
    SMITH_LANG_WORD("_BitInt", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Bool", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Complex", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Decimal128", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Decimal32", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Decimal64", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Float128", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Float16", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Float32", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Float32x", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Float64", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Float64x", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Generic", SmithLangWord_Keyword),
    SMITH_LANG_WORD("_Imaginary", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("_Noreturn", SmithLangWord_Storage),
    SMITH_LANG_WORD("_Static_assert", SmithLangWord_Keyword),
    SMITH_LANG_WORD("_Thread_local", SmithLangWord_Storage),
    SMITH_LANG_WORD("__asm", SmithLangWord_Keyword),
    SMITH_LANG_WORD("__asm__", SmithLangWord_Keyword),
    SMITH_LANG_WORD("__attribute__", SmithLangWord_Keyword),
    SMITH_LANG_WORD("__auto_type", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("__bf16", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("__builtin_offsetof", SmithLangWord_Keyword),
    SMITH_LANG_WORD("__builtin_types_compatible_p", SmithLangWord_Keyword),
    SMITH_LANG_WORD("__builtin_va_arg", SmithLangWord_Keyword),
    SMITH_LANG_WORD("__const", SmithLangWord_Qualifier),
    SMITH_LANG_WORD("__extension__", SmithLangWord_Keyword),
    SMITH_LANG_WORD("__fp16", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("__inline", SmithLangWord_Storage),
    SMITH_LANG_WORD("__inline__", SmithLangWord_Storage),
    SMITH_LANG_WORD("__int128", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("__restrict", SmithLangWord_Qualifier),
    SMITH_LANG_WORD("__restrict__", SmithLangWord_Qualifier),
    SMITH_LANG_WORD("__typeof__", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("__volatile__", SmithLangWord_Qualifier),
    SMITH_LANG_WORD("alignas", SmithLangWord_Keyword),
    SMITH_LANG_WORD("alignof", SmithLangWord_Keyword),
    SMITH_LANG_WORD("asm", SmithLangWord_Keyword),
    SMITH_LANG_WORD("auto", SmithLangWord_Storage),
    SMITH_LANG_WORD("bool", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("break", SmithLangWord_Keyword),
    SMITH_LANG_WORD("case", SmithLangWord_Keyword),
    SMITH_LANG_WORD("char", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("const", SmithLangWord_Qualifier),
    SMITH_LANG_WORD("constexpr", SmithLangWord_Storage),
    SMITH_LANG_WORD("continue", SmithLangWord_Keyword),
    SMITH_LANG_WORD("default", SmithLangWord_Keyword),
    SMITH_LANG_WORD("do", SmithLangWord_Keyword),
    SMITH_LANG_WORD("double", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("else", SmithLangWord_Keyword),
    SMITH_LANG_WORD("enum", SmithLangWord_Tag),
    SMITH_LANG_WORD("extern", SmithLangWord_Storage),
    SMITH_LANG_WORD("false", SmithLangWord_Keyword),
    SMITH_LANG_WORD("float", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("for", SmithLangWord_Keyword),
    SMITH_LANG_WORD("goto", SmithLangWord_Keyword),
    SMITH_LANG_WORD("if", SmithLangWord_Keyword),
    SMITH_LANG_WORD("inline", SmithLangWord_Storage),
    SMITH_LANG_WORD("int", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("long", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("noreturn", SmithLangWord_Storage),
    SMITH_LANG_WORD("nullptr", SmithLangWord_Keyword),
    SMITH_LANG_WORD("register", SmithLangWord_Storage),
    SMITH_LANG_WORD("restrict", SmithLangWord_Qualifier),
    SMITH_LANG_WORD("return", SmithLangWord_Keyword),
    SMITH_LANG_WORD("short", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("signed", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("sizeof", SmithLangWord_Keyword),
    SMITH_LANG_WORD("static", SmithLangWord_Storage),
    SMITH_LANG_WORD("static_assert", SmithLangWord_Keyword),
    SMITH_LANG_WORD("struct", SmithLangWord_Tag),
    SMITH_LANG_WORD("switch", SmithLangWord_Keyword),
    SMITH_LANG_WORD("thread_local", SmithLangWord_Storage),
    SMITH_LANG_WORD("true", SmithLangWord_Keyword),
    SMITH_LANG_WORD("typedef", SmithLangWord_Typedef),
    SMITH_LANG_WORD("typeof", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("typeof_unqual", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("union", SmithLangWord_Tag),
    SMITH_LANG_WORD("unsigned", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("void", SmithLangWord_TypeKeyword),
    SMITH_LANG_WORD("volatile", SmithLangWord_Qualifier),
    SMITH_LANG_WORD("while", SmithLangWord_Keyword),
};

const SmithLangSized smith_lang_sized[] = {
    {"F16", "_Float16", "_Float16"},
    {"F32", "float", "float"},
    {"F64", "double", "double"},
    {"I16", "__INT16_TYPE__", "int16_t"},
    {"I32", "__INT32_TYPE__", "int32_t"},
    {"I64", "__INT64_TYPE__", "int64_t"},
    {"I8", "__INT8_TYPE__", "int8_t"},
    {"Isize", "__PTRDIFF_TYPE__", "ptrdiff_t"},
    {"U16", "__UINT16_TYPE__", "uint16_t"},
    {"U32", "__UINT32_TYPE__", "uint32_t"},
    {"U64", "__UINT64_TYPE__", "uint64_t"},
    {"U8", "__UINT8_TYPE__", "uint8_t"},
    {"Usize", "__SIZE_TYPE__", "size_t"},
};

// Whether the file has tuples (the text has one, or an import's header does): the host offers every token, and `t[0]`
// and `countof(t)` are asked of the compiler (runtime/lang_tuple.c).
bool smith_lang_tuples_on(const SmithLang *lang, const SmithHost *host) {
    return lang->tuples_seen || host->maybe_tuple;
}

// Whether the file has type values (the text has one, or an import has functions over `type`, issue #107): the host
// offers every token and `runtime/lang_type.c` reads them.
bool smith_lang_type_on(const SmithLang *lang, const SmithHost *host) {
    (void)lang;
    return host->maybe_type_value;
}

// ---- Small helpers ----------------------------------------------------------------------------------------------

[[gnu::always_inline]] bool smith_lang_equals(const char *text, size_t length, const char *literal) {
    size_t expected = strlen(literal);
    return length == expected && memcmp(text, literal, expected) == 0;
}

[[gnu::always_inline]] static inline bool smith_lang_is(const SmithToken *token, SmithTokenKind kind,
                                                        const char *literal) {
    return token->kind == kind && smith_lang_equals(token->text, token->length, literal);
}

[[gnu::always_inline]] bool smith_lang_punct(const SmithToken *token, const char *literal) {
    return smith_lang_is(token, SmithTokenKind_Punctuator, literal);
}

[[gnu::always_inline]] bool smith_lang_ident(const SmithToken *token, const char *literal) {
    return smith_lang_is(token, SmithTokenKind_Identifier, literal);
}

// The table is sorted, so the words that start with one character are together: [first, last) per character, made once.
static uint8_t smith_lang_word_first[256];
static uint8_t smith_lang_word_last[256];

void smith_lang_word_buckets(void) {
    if (smith_lang_word_last[(uint8_t)'w'] != 0) return; // "while" is the last entry: the table is made
    size_t count = sizeof smith_lang_words / sizeof smith_lang_words[0];
    for (size_t i = 0; i < count; i++) { // the table
        uint8_t c = (uint8_t)smith_lang_words[i].text[0];
        if (smith_lang_word_last[c] == 0) smith_lang_word_first[c] = (uint8_t)i;
        smith_lang_word_last[c] = (uint8_t)(i + 1);
    }
}

SmithLangWord smith_lang_word(const char *text, size_t length) {
    if (length == 0) return SmithLangWord_Other;
    uint8_t c = (uint8_t)text[0];
    for (size_t i = smith_lang_word_first[c]; i < smith_lang_word_last[c]; i++) { // the words that start with c
        const SmithLangWordEntry *entry = &smith_lang_words[i];
        if (entry->length == length && memcmp(text + 1, entry->text + 1, length - 1) == 0) return entry->word;
    }
    return SmithLangWord_Other;
}

bool smith_lang_is_word(const SmithToken *token) {
    return token->kind == SmithTokenKind_Identifier &&
           smith_lang_word(token->text, token->length) != SmithLangWord_Other;
}

// ---- Capitalized type names (issue #366) --------------------------------------------------------------------------
//
// Types are written with a capital (`I32`, `Str`, `Type`, `Option(T)`, the concepts `Int` and `Num`), and the lowering
// knows them by those names: a lowercase `i32` is the program's own name. The one exception is C's own keyword: `Bool`
// is read as `bool`, the type C23 spells with a lowercase keyword.

typedef struct {
    const char *written;
    const char *name;
} SmithLangCapital;

static const SmithLangCapital smith_lang_capitals[] = {
    {"Bool", "bool"},
    {"Char", "char"},
};

// The capitalized name number `index` and the lowercase name it stands for; false past the last.
bool smith_lang_capital_at(size_t index, const char **written, const char **name) {
    if (index >= sizeof smith_lang_capitals / sizeof smith_lang_capitals[0]) return false;
    *written = smith_lang_capitals[index].written;
    *name = smith_lang_capitals[index].name;
    return true;
}

// The lowercase name a capitalized one stands for, or null.
const char *smith_lang_capital_name(const char *text, size_t length) {
    if (length < 2 || length > 8 || text[0] < 'A' || text[0] > 'Z') return nullptr;
    for (size_t i = 0; i < sizeof smith_lang_capitals / sizeof smith_lang_capitals[0]; i++) { // 1 name
        if (smith_lang_equals(text, length, smith_lang_capitals[i].written)) return smith_lang_capitals[i].name;
    }
    return nullptr;
}

// Writes a source text into `out` (room for `length * 3 + 1`) with the lowercase names in place of the capitalized
// ones: the same length for every type, a longer name for a concept when `concepts` is set. Comments and strings are
// copied as they are, and so is a name after `.`, `->` or `::` (a member or a C name). Returns the length written.
size_t smith_lang_capitals_lower(const char *text, size_t length, char *out, bool concepts) {
    size_t used = 0;
    for (size_t at = 0; at < length;) {                      // each piece of the text once
        size_t end = smith_lang_text_skip(text, length, at); // a comment or a literal
        if (end == at) {
            end = at + 1;
            while (smith_lang_word_char(text[at]) && end < length && smith_lang_word_char(text[end]))
                end += 1;
        }
        const char *name = smith_lang_capital_name(text + at, end - at);
        bool member = at > 0 && (text[at - 1] == '.' || (at > 1 && text[at - 1] == '>' && text[at - 2] == '-') ||
                                 (at > 1 && text[at - 1] == ':' && text[at - 2] == ':'));
        if (name != nullptr && !member && (concepts || strlen(name) == end - at)) {
            memcpy(out + used, name, strlen(name));
            used += strlen(name);
        } else {
            memmove(out + used, text + at, end - at);
            used += end - at;
        }
        at = end;
    }
    out[used] = 0;
    return used;
}

const SmithLangSized *smith_lang_find_sized(const char *text, size_t length) {
    for (size_t i = 0; i < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; i++) { // 13 entries
        if (smith_lang_equals(text, length, smith_lang_sized[i].name)) return &smith_lang_sized[i];
    }
    return nullptr;
}

SmithLangText smith_lang_store(SmithLang *lang, const char *text, size_t length) {
    if (lang->chars_used + length + 1 > smith_lang_chars_max) {
        lang->broken = true;
        return (SmithLangText){0, 0};
    }
    memcpy(smith_lang_chars + lang->chars_used, text, length);
    smith_lang_chars[lang->chars_used + length] = 0;
    SmithLangText stored = {(uint32_t)lang->chars_used, (uint32_t)length};
    lang->chars_used += length + 1;
    return stored;
}

const char *smith_lang_str(SmithLangText text) {
    return smith_lang_chars + text.offset;
}

// The output, the declarations and the rest, in the order they need each other (a comment between them keeps
// clang-format from sorting the includes).

// The comments and conditional directives a module's header keeps.

// Names, bindings and the lowering of one identifier.

// The names as the source writes them.

// The module line and the imports.

// The head of a file-scope declaration.

// Generic functions: `type T` parameters, their calls and their instances.

// The module `target`, and `if constexpr` outside generics.

// A file that is not a module, with the host offering few tokens.

// A module's private declarations, read as they stream by.

// Tuples: `struct { i32, f32 }`, `t[0]`, `countof(t)`.

// `try` and `errdefer` for `result` and `option`.

// Lambdas that capture nothing, lifted out of their function.

// `_` and `_name` mean unused; `_ = expr;` discards a value.

// Run-time arrays: `T xs[:]`, `xs[i]`, `xs[a:b]`.

// Range `for`: `for (T x : xs) { ... }`.

// Enums with payloads: `typedef enum { Circle circle; empty; } Shape;`, `.empty`, `case .circle:`.

// Types as values: `constexpr Type t = i32;`, a type handle, and what is asked of one.

// Bodies, the start and end of the file, and the entry points.

// The private names an exported generic's body uses.

// The bodies of a module's functions, for an importer that runs them at compile time.

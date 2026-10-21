// The lowering of .smith files: C with `module`, `import` and `export`, and sized type names, turned into C. The
// language is described in docs/smith-lang.md; this file is the state machine, written against runtime/lang.h so that
// it knows nothing of clang. It is included by smith.c.
//
// It is given the tokens of one file in order, after macro expansion, and says what replaces each. Its work:
//   - `module a.b;` and `import ...;` lines are consumed. Imports bind a name (`rp.open` becomes `game_replay_open`),
//     and `import <h>;` includes the header, whose names are then reached as `c.name`.
//   - In a module, a file-scope declaration is read whole (its head: everything up to the `;`, `=` or `{`), because
//     what it needs depends on all of it: a prefix on the names it declares (`game_replay_open`), `static` on a private
//     function or variable, a prototype with the dotted symbol (an `__asm__` label) for an exported function.
//   - Sized type names become the standard types.
//   - Every exported declaration is also written, as C, to the module's generated header.
// Nothing here allocates: the tables are fixed, and when one fills the file is passed through unchanged after an error.

#include "lang.h"

constexpr size_t smith_lang_names_max = 16384;
constexpr size_t smith_lang_slots = 32768; // a power of two, twice the names
constexpr size_t smith_lang_chars_max = (size_t)1 << 20;
constexpr size_t smith_lang_scratch_max = (size_t)1 << 18;
constexpr size_t smith_lang_head_max = 4096;
constexpr size_t smith_lang_queue_max = 32;
constexpr size_t smith_lang_batch_max = 4096;
constexpr size_t smith_lang_braces_max = 256;
constexpr size_t smith_lang_bindings_max = 128;
constexpr size_t smith_lang_imports_max = 128;
constexpr size_t smith_lang_header_max = (size_t)1 << 20;
constexpr size_t smith_lang_dump_max = (size_t)1 << 22;
constexpr size_t smith_lang_allowed_max = 4096;
constexpr size_t smith_lang_name_max = 256;
constexpr size_t smith_lang_message_max = 512;
constexpr size_t smith_lang_cycle_max = 64;

typedef enum {
    SmithLangWord_Other,
    SmithLangWord_TypeKeyword,
    SmithLangWord_Qualifier,
    SmithLangWord_Storage,
    SmithLangWord_Tag, // struct, union, enum
    SmithLangWord_Typedef,
    SmithLangWord_Keyword,
} SmithLangWord;

typedef enum {
    SmithLangName_Type,
    SmithLangName_Tag,
    SmithLangName_Value,
    SmithLangName_Function,
    SmithLangName_Constant,
} SmithNameKind;

typedef enum {
    SmithLangState_Start,  // between declarations, at file scope
    SmithLangState_Head,   // reading the head of a file-scope declaration (modules only)
    SmithLangState_Stream, // anything else: a body, an initializer, what follows an aggregate
} SmithLangState;

typedef enum {
    SmithLangBrace_Block,
    SmithLangBrace_Function,
    SmithLangBrace_Aggregate,
    SmithLangBrace_Enum,
    SmithLangBrace_Init,
} SmithLangBrace;

typedef enum {
    SmithLangRole_Plain,
    SmithLangRole_Declared, // the name a declaration introduces
    SmithLangRole_Done,     // lowered already (a namespace access, a sized type)
} SmithLangRole;

typedef struct {
    uint32_t offset;
    uint32_t length;
} SmithLangText;

typedef struct {
    SmithLangText name;
    SmithNameKind kind;
    bool exported;
} SmithLangName;

typedef struct {
    SmithLangText name;
    SmithLangText prefix; // game_replay_
    SmithLangText module; // game.replay
    bool is_c;
    bool shadowed; // a declaration reused the name (reported): from then on it is the declaration's
} SmithLangBinding;

// A token of a declaration head, held until the declaration is known.
typedef struct {
    SmithTokenKind kind;
    SmithLangRole role;
    bool line_start;
    bool space_before;
    bool expand;
    bool after_member;
    bool after_tag;
    bool typeish;
    char before; // the first character of the token before, when that was a punctuator
    uint32_t location;
    uint32_t paren_depth;
    SmithLangText text;
    SmithLangText header_text;
    SmithLangText original; // the name as written, before it was prefixed
} SmithLangHeld;

typedef struct {
    bool exported;
    bool is_typedef;
    bool storage; // static or extern
    bool is_constexpr;
    bool aggregate;     // struct, union or enum appeared
    bool saw_type;      // a type was named
    bool is_function;   // a declarator is followed by (
    bool name_found;    // the current declarator has its name
    bool in_params;     // inside the parameter list of the declared function
    bool params_next;   // the next ( opens it
    bool last_tag_enum; // the last struct, union or enum keyword was enum
    bool checking;      // names in this declaration are checked for private types
    uint32_t declared;  // how many names the head declares
    uint32_t last_name; // the held token of the last one
} SmithLangDecl;

struct SmithLang {
    bool broken;
    bool started;
    bool finished;
    const char *main_file;
    size_t header_full_used;
    size_t pending;      // the held identifier waiting for the token after it (SIZE_MAX: none)
    bool pending_export; // `export` was read, and the declaration it applies to is next
    uint32_t bracket_depth;
    char prev_name[64]; // the last token, if it was a plain name: it may be a type the parser knows
    size_t prev_name_length;
    unsigned errors;
    bool is_module;
    bool has_c;
    bool seen_declaration;
    bool seen_module_line;
    uint32_t imports_seen;
    char module[smith_lang_name_max];
    char prefix[smith_lang_name_max];
    char symbol[smith_lang_name_max];
    SmithLangState state;
    SmithLangDecl decl;
    // Where in the file.
    uint32_t brace_depth;
    SmithLangBrace braces[smith_lang_braces_max];
    uint32_t paren_depth;
    bool in_function; // inside a function body: nothing declared here is the module's
    bool agg_pending; // struct, union or enum seen, `{` not yet
    bool enum_pending;
    bool init_top;          // an initializer at file scope is being read
    bool tail_declarator;   // after an aggregate's `}` or a comma: names are declarators
    bool statement_typedef; // `typedef` seen in this statement
    // The token before, and the type tracking.
    bool prev_member;
    bool prev_tag;
    bool typeish;
    char prev_char;
    // Lookahead and the batch.
    SmithToken queue[smith_lang_queue_max];
    size_t queue_head;
    size_t queue_count;
    size_t batch_in;
    size_t out_count;
    bool changed;
    bool pending_kept_set;
    SmithToken pending_kept;
    bool carry_line_start;
    // The head.
    SmithLangHeld head[smith_lang_head_max];
    size_t head_count;
    size_t scratch_used;
    // Tables.
    SmithLangName names[smith_lang_names_max];
    size_t names_count;
    uint32_t slots[smith_lang_slots];
    SmithLangBinding bindings[smith_lang_bindings_max];
    size_t bindings_count;
    size_t chars_used;
    // Generated header and dump.
    size_t imports_count;
    SmithLangText smith_imports[smith_lang_imports_max];
    SmithLangText c_imports[smith_lang_imports_max];
    bool c_import_angled[smith_lang_imports_max];
    size_t c_imports_count;
    bool export_active;
    size_t header_used;
    bool dump_enabled;
    size_t dump_used;
    bool dump_bol;
    uint32_t allowed[smith_lang_allowed_max];
    size_t allowed_count;
    char tmp[smith_lang_name_max * 2];
    size_t tmp_length;
};

static char smith_lang_chars[smith_lang_chars_max];
static char smith_lang_scratch[smith_lang_scratch_max];
static char smith_lang_header_body[smith_lang_header_max];
static char smith_lang_header_full[smith_lang_header_max + 4096];
static char smith_lang_dump[smith_lang_dump_max];
static SmithLang smith_lang_instance;

typedef struct {
    const char *text;
    SmithLangWord word;
} SmithLangWordEntry;

static const SmithLangWordEntry smith_lang_words[] = {
    {"_Alignas", SmithLangWord_Keyword},
    {"_Alignof", SmithLangWord_Keyword},
    {"_Atomic", SmithLangWord_Qualifier},
    {"_BitInt", SmithLangWord_TypeKeyword},
    {"_Bool", SmithLangWord_TypeKeyword},
    {"_Complex", SmithLangWord_TypeKeyword},
    {"_Decimal128", SmithLangWord_TypeKeyword},
    {"_Decimal32", SmithLangWord_TypeKeyword},
    {"_Decimal64", SmithLangWord_TypeKeyword},
    {"_Float128", SmithLangWord_TypeKeyword},
    {"_Float16", SmithLangWord_TypeKeyword},
    {"_Float32", SmithLangWord_TypeKeyword},
    {"_Float32x", SmithLangWord_TypeKeyword},
    {"_Float64", SmithLangWord_TypeKeyword},
    {"_Float64x", SmithLangWord_TypeKeyword},
    {"_Generic", SmithLangWord_Keyword},
    {"_Imaginary", SmithLangWord_TypeKeyword},
    {"_Noreturn", SmithLangWord_Storage},
    {"_Static_assert", SmithLangWord_Keyword},
    {"_Thread_local", SmithLangWord_Storage},
    {"__asm", SmithLangWord_Keyword},
    {"__asm__", SmithLangWord_Keyword},
    {"__attribute__", SmithLangWord_Keyword},
    {"__auto_type", SmithLangWord_TypeKeyword},
    {"__bf16", SmithLangWord_TypeKeyword},
    {"__builtin_offsetof", SmithLangWord_Keyword},
    {"__builtin_types_compatible_p", SmithLangWord_Keyword},
    {"__builtin_va_arg", SmithLangWord_Keyword},
    {"__const", SmithLangWord_Qualifier},
    {"__extension__", SmithLangWord_Keyword},
    {"__fp16", SmithLangWord_TypeKeyword},
    {"__inline", SmithLangWord_Storage},
    {"__inline__", SmithLangWord_Storage},
    {"__int128", SmithLangWord_TypeKeyword},
    {"__restrict", SmithLangWord_Qualifier},
    {"__restrict__", SmithLangWord_Qualifier},
    {"__typeof__", SmithLangWord_TypeKeyword},
    {"__volatile__", SmithLangWord_Qualifier},
    {"alignas", SmithLangWord_Keyword},
    {"alignof", SmithLangWord_Keyword},
    {"asm", SmithLangWord_Keyword},
    {"auto", SmithLangWord_Storage},
    {"bool", SmithLangWord_TypeKeyword},
    {"break", SmithLangWord_Keyword},
    {"case", SmithLangWord_Keyword},
    {"char", SmithLangWord_TypeKeyword},
    {"const", SmithLangWord_Qualifier},
    {"constexpr", SmithLangWord_Storage},
    {"continue", SmithLangWord_Keyword},
    {"default", SmithLangWord_Keyword},
    {"do", SmithLangWord_Keyword},
    {"double", SmithLangWord_TypeKeyword},
    {"else", SmithLangWord_Keyword},
    {"enum", SmithLangWord_Tag},
    {"extern", SmithLangWord_Storage},
    {"false", SmithLangWord_Keyword},
    {"float", SmithLangWord_TypeKeyword},
    {"for", SmithLangWord_Keyword},
    {"goto", SmithLangWord_Keyword},
    {"if", SmithLangWord_Keyword},
    {"inline", SmithLangWord_Storage},
    {"int", SmithLangWord_TypeKeyword},
    {"long", SmithLangWord_TypeKeyword},
    {"noreturn", SmithLangWord_Storage},
    {"nullptr", SmithLangWord_Keyword},
    {"register", SmithLangWord_Storage},
    {"restrict", SmithLangWord_Qualifier},
    {"return", SmithLangWord_Keyword},
    {"short", SmithLangWord_TypeKeyword},
    {"signed", SmithLangWord_TypeKeyword},
    {"sizeof", SmithLangWord_Keyword},
    {"static", SmithLangWord_Storage},
    {"static_assert", SmithLangWord_Keyword},
    {"struct", SmithLangWord_Tag},
    {"switch", SmithLangWord_Keyword},
    {"thread_local", SmithLangWord_Storage},
    {"true", SmithLangWord_Keyword},
    {"typedef", SmithLangWord_Typedef},
    {"typeof", SmithLangWord_TypeKeyword},
    {"typeof_unqual", SmithLangWord_TypeKeyword},
    {"union", SmithLangWord_Tag},
    {"unsigned", SmithLangWord_TypeKeyword},
    {"void", SmithLangWord_TypeKeyword},
    {"volatile", SmithLangWord_Qualifier},
    {"while", SmithLangWord_Keyword},
};

typedef struct {
    const char *name;
    const char *standard; // what the compiler sees: the compiler's own name for the type, so no header is needed
    const char *header;   // what a generated header says
} SmithLangSized;

static const SmithLangSized smith_lang_sized[] = {
    {"f16", "_Float16", "_Float16"},
    {"f32", "float", "float"},
    {"f64", "double", "double"},
    {"i16", "__INT16_TYPE__", "int16_t"},
    {"i32", "__INT32_TYPE__", "int32_t"},
    {"i64", "__INT64_TYPE__", "int64_t"},
    {"i8", "__INT8_TYPE__", "int8_t"},
    {"isize", "__PTRDIFF_TYPE__", "ptrdiff_t"},
    {"u16", "__UINT16_TYPE__", "uint16_t"},
    {"u32", "__UINT32_TYPE__", "uint32_t"},
    {"u64", "__UINT64_TYPE__", "uint64_t"},
    {"u8", "__UINT8_TYPE__", "uint8_t"},
    {"usize", "__SIZE_TYPE__", "size_t"},
};

// ---- Small helpers ----------------------------------------------------------------------------------------------

static bool smith_lang_equals(const char *text, size_t length, const char *literal) {
    size_t expected = strlen(literal);
    return length == expected && memcmp(text, literal, expected) == 0;
}

static bool smith_lang_is(const SmithToken *token, SmithTokenKind kind, const char *literal) {
    return token->kind == kind && smith_lang_equals(token->text, token->length, literal);
}

static bool smith_lang_punct(const SmithToken *token, const char *literal) {
    return smith_lang_is(token, SmithTokenKind_Punctuator, literal);
}

static bool smith_lang_ident(const SmithToken *token, const char *literal) {
    return smith_lang_is(token, SmithTokenKind_Identifier, literal);
}

static SmithLangWord smith_lang_word(const char *text, size_t length) {
    size_t low = 0;
    size_t high = sizeof smith_lang_words / sizeof smith_lang_words[0];
    while (low < high) { // a binary search: bounded by log2 of the table
        size_t middle = low + (high - low) / 2;
        const char *entry = smith_lang_words[middle].text;
        size_t entry_length = strlen(entry);
        int order = memcmp(text, entry, length < entry_length ? length : entry_length);
        if (order == 0) order = length < entry_length ? -1 : length > entry_length ? 1 : 0;
        if (order == 0) return smith_lang_words[middle].word;
        if (order < 0) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    return SmithLangWord_Other;
}

static bool smith_lang_is_word(const SmithToken *token) {
    return token->kind == SmithTokenKind_Identifier &&
           smith_lang_word(token->text, token->length) != SmithLangWord_Other;
}

static const SmithLangSized *smith_lang_find_sized(const char *text, size_t length) {
    for (size_t i = 0; i < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; i++) { // 13 entries
        if (smith_lang_equals(text, length, smith_lang_sized[i].name)) return &smith_lang_sized[i];
    }
    return nullptr;
}

static SmithLangText smith_lang_store(SmithLang *lang, const char *text, size_t length) {
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

static const char *smith_lang_str(SmithLangText text) {
    return smith_lang_chars + text.offset;
}

// The output, the declarations and the rest, in the order they need each other (a comment between them keeps
// clang-format from sorting the includes).
#include "lang_out.c"

// Names, bindings and the lowering of one identifier.
#include "lang_decl.c"

// The module line and the imports.
#include "lang_import.c"

// The head of a file-scope declaration.
#include "lang_head.c"

// Bodies, the start and end of the file, and the entry points.
#include "lang_stream.c"

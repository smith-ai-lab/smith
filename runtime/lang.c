// The lowering of .smith files: C with `module`, `import` and `export`, and sized type names, turned into C. The
// language is described in docs/smith-lang.md; this file is the state machine, written against runtime/lang.h so that
// it knows nothing of clang. It is included by smith.c.
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

#include "lang.h"

constexpr size_t smith_lang_names_max = 65536;
constexpr size_t smith_lang_slots = 131072; // a power of two, twice the names
constexpr size_t smith_lang_chars_max = (size_t)1 << 20;
constexpr size_t smith_lang_scratch_max = (size_t)1 << 18;
constexpr size_t smith_lang_head_max = 4096;
constexpr size_t smith_lang_queue_max = 256;
constexpr size_t smith_lang_batch_max = 4096;
constexpr size_t smith_lang_call_sites_max = 2048; // calls to generics the editor can be told of
constexpr size_t smith_lang_call_frames_max = 16;  // generic calls inside generic calls' arguments
constexpr size_t smith_lang_braces_max = 256;
constexpr size_t smith_lang_bindings_max = 128;
constexpr size_t smith_lang_imports_max = 128;
constexpr size_t smith_lang_header_max = (size_t)1 << 20;
constexpr size_t smith_lang_dump_max = (size_t)1 << 22;
constexpr size_t smith_lang_allowed_max = 16; // the macro names c::X lets expand, until they have
constexpr size_t smith_lang_name_max = 256;
constexpr size_t smith_lang_params_max = 16;
constexpr size_t smith_lang_param_max = 64;
constexpr size_t smith_lang_message_max = 512;
constexpr size_t smith_lang_cycle_max = 64;
constexpr size_t smith_lang_written_max = 8192;
constexpr size_t smith_lang_written_slots = 16384; // a power of two, twice the names
constexpr size_t smith_lang_transitive_max = 32;
constexpr size_t smith_lang_cond_depth_max = 64;      // #if nested in a module the header pass reads
constexpr size_t smith_lang_cond_lines_max = 32;      // lines of one chain: #if, #elif..., #else
constexpr size_t smith_lang_cnames_max = 1024;        // exports of imported modules that have a C name of their own
constexpr size_t smith_lang_gt_max = (size_t)1 << 17; // tokens of generics, and of the calls being read
constexpr size_t smith_lang_gt_text_max = (size_t)1 << 21; // their text
constexpr size_t smith_lang_it_max = (size_t)1 << 15;      // tokens of the types instances were asked for
constexpr size_t smith_lang_it_text_max = (size_t)1 << 19; // their text
constexpr size_t smith_lang_generics_max = 256;            // generics known: the module's own and the imports'
constexpr size_t smith_lang_instances_max = 4096;          // instances asked for in one file
constexpr size_t smith_lang_type_params_max = 4;           // `type T` parameters of one generic
constexpr size_t smith_lang_call_params_max = 16;          // parameters of one generic
constexpr size_t smith_lang_instance_depth_max = 32;       // generics asked for by the instances of generics
constexpr size_t smith_lang_generic_modules_max = 64;      // modules whose generics were read
constexpr size_t smith_lang_generic_scan_max = 200;        // tokens read ahead to see whether a declaration is generic

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
} SmithLangNameKind;

typedef enum {
    SmithLangState_Start,   // between declarations, at file scope
    SmithLangState_Head,    // reading the head of a file-scope declaration (modules only), held until it is known
    SmithLangState_Declare, // reading the head of a private declaration as it streams by (a sparse host)
    SmithLangState_Stream,  // anything else: a body, an initializer, what follows an aggregate
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

// A call to a generic being read (runtime/lang_generic.c): where it began in the pool, and the brackets seen so far.
typedef struct {
    uint32_t location; // of the callee's name in the file
    uint32_t generic;
    uint32_t instance;
} SmithLangCallSite;

typedef struct {
    bool record; // in a generic's body: kept whole between marks, not understood yet
    bool skip;   // the next token shown to it is the one the call began at
    uint32_t generic_index;
    uint32_t location;
    uint32_t member_location;
    uint32_t count; // smith_lang_gt.count and text_used when it began
    uint32_t text;
    int depth;
    uint64_t braces; // per open bracket (bit 0 is the first): a `{`, else a `(` or `[`
} SmithLangCallFrame;

typedef struct {
    uint32_t offset;
    uint32_t length;
} SmithLangText;

typedef struct {
    SmithLangText name;
    SmithLangNameKind kind;
    bool exported;
    uint32_t location;    // of the declaration's name in the source, in the host's own measure
    SmithLangText c_name; // the C name `[[smith::c_name("...")]]` chose for an export (length 0: the prefixed name)
    uint16_t generic;     // 1 + the index of the generic function this name is (0: it is not one)
} SmithLangName;

// One token a generic keeps (a function with `type T` parameters, runtime/lang_generic.c), or of the calls being read
// and the types instances were asked for. 28 bytes; the text is in the pool's own.
typedef enum {
    SmithLangGen_Plain,
    SmithLangGen_CallBegin, // a call that may be to a generic: `module::member`, then the whole call, then CallEnd
    SmithLangGen_CallEnd,
    SmithLangGen_IfConstexpr, // `if constexpr`: the condition and the branch that is kept are chosen for each instance
} SmithLangGenSpecial;

typedef struct {
    uint8_t kind;    // SmithTokenKind
    uint8_t special; // SmithLangGenSpecial
    uint8_t param;   // 1 + the type parameter the identifier names (0: none)
    uint8_t flags;   // line start, space before, expand, name
    uint32_t text;   // offset in the pool's text
    uint32_t length;
    uint32_t location; // the host's; UINT32_MAX when the token was read from a header and not placed yet
    uint32_t line;     // in the source of the generic, for a token read from a header (0: location is the host's)
    uint32_t column;
} SmithLangGenToken;

typedef struct {
    SmithLangText module;  // game::replay
    SmithLangText member;  // push
    SmithLangText lowered; // game_replay_push
    SmithLangText source;  // the file of the module, to place tokens read from a header
    uint32_t first;        // the tokens in smith_lang_gt: the head, then the body
    uint32_t count;
    uint32_t name_index; // of the tokens: the name, `(`, the matching `)`, and the `{` of the body
    uint32_t open_index;
    uint32_t close_index;
    uint32_t body_index;
    uint8_t param_count;
    uint8_t type_count;
    uint8_t type_token[smith_lang_type_params_max];    // the T of each `type T`: the index of its token in the head
    uint8_t type_position[smith_lang_type_params_max]; // and which parameter it is
    bool is_inline;
    bool exported;
    bool own; // the module being lowered defines it (else an import's)
} SmithLangGeneric;

// A name the compiler sees, and how the source writes it (runtime/lang_names.c).
typedef struct {
    SmithLangText lowered;  // geometry_box_Box
    SmithLangText written;  // box::Box (Box in the module itself)
    SmithLangText module;   // geometry::box
    uint16_t prefix_length; // of lowered: what is left is the member
    SmithLangText member;   // the member, when lowered is a chosen C name and not prefix + member (length 0: derived)
    bool direct;
} SmithLangWritten;

// An #if of the module, with its #elif and #else lines so far: kept until an export inside it needs them in the header.
typedef struct {
    const SmithTrivia *lines[smith_lang_cond_lines_max];
    uint8_t count;
    bool emitted;     // the lines so far are in the header, and so are the rest as they come
    bool zero;        // `#if 0`, no branch of it live yet
    bool transparent; // `#if 0 ... #else`: the live branch is plain text, and the header has no conditional for it
    bool first_open;  // lines[0] is an #elif that stands for the #if of the chain (after `#if 0`)
} SmithLangCond;

// An export of an imported module that has a C name of its own: the name the prefix and the member would make, and the
// name the module's header gives it.
typedef struct {
    SmithLangText fallback;
    SmithLangText c_name;
} SmithLangCName;

typedef struct {
    SmithLangText name;
    SmithLangText prefix; // game_replay_
    SmithLangText module; // game::replay
    bool is_c;
    bool missing; // the module could not be imported: its names are not checked, the import was the one error
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
    uint32_t handle; // the host's token, kept while the head is held; none once the text is changed
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
    bool aggregate;       // struct, union or enum appeared
    bool saw_type;        // a type was named
    bool is_function;     // a declarator is followed by (
    bool name_found;      // the current declarator has its name
    bool in_params;       // inside the parameter list of the declared function
    bool is_extern;       // `extern` appeared
    bool is_inline;       // `inline` appeared
    bool inline_header;   // an exported inline function: its body goes to the header too
    bool params_next;     // the next ( opens it
    bool last_tag_enum;   // the last struct, union or enum keyword was enum
    bool checking;        // names in this declaration are checked for private types
    uint32_t declared;    // how many names the head declares
    uint32_t last_name;   // the held token of the last one
    bool prev_star;       // the token before was *
    bool prev_tag;        // the token before was struct, union or enum
    bool static_inserted; // `static` was written before the first token, so a later one is dropped
    bool attribute_first; // the declaration begins with [[ ]]: `static` goes after them, before the token that follows
    SmithLangText c_name; // [[smith::c_name("...")]] before this export (length 0: none)
    uint32_t c_name_location;
    bool c_name_used; // a name this declaration introduces took it
    bool generic;     // the head has a `type T` parameter: the declaration is recorded, not compiled
} SmithLangDecl;

struct SmithLang {
    bool broken;
    bool broken_reported;
    bool started;
    bool finished;
    const char *main_file;
    size_t header_full_used;
    size_t pending;               // the held identifier waiting for the token after it (SIZE_MAX: none)
    bool pending_export;          // `export` was read, and the declaration it applies to is next
    SmithLangText pending_c_name; // [[smith::c_name("...")]] was read: the export after it has that C name
    uint32_t pending_c_name_location;
    bool pending_c_name_line; // the attribute was the first token of its line
    SmithLangCond conds[smith_lang_cond_depth_max];
    uint32_t cond_depth;
    uint32_t cond_overflow;          // conditionals open beyond the table
    bool cond_reported;              // the error "nest more deeply" was given
    const SmithHost *host_now;       // the host of the token being fed, for errors in the header's trivia
    const SmithTrivia *trivia_first; // the first entry of the file
    const SmithTrivia *trivia_last;  // the last entry applied
    const SmithTrivia *doc_from;     // the comments of the gap before the token taken last (a doc comment, maybe)
    const SmithTrivia *doc_to;
    const SmithTrivia *doc_taken_from; // the same, for the token taken last, once the gap is closed
    const SmithTrivia *doc_taken_to;
    bool doc_pending; // an export is next and has a doc comment (doc_export_from .. doc_export_to)
    const SmithTrivia *doc_export_from;
    const SmithTrivia *doc_export_to;
    bool export_tail;    // the header has the export's end, though export_active is off (a variable, a prototype)
    bool tail_open;      // an export just ended in the header: a comment after it on its line follows it there
    bool unnamed_c_name; // an export ended that took no name from its [[smith::c_name]]
    uint32_t unnamed_c_name_location;
    size_t c_name_count; // how many of the module's own names have a C name chosen
    SmithLangCName cnames[smith_lang_cnames_max];
    size_t cnames_count;
    uint32_t bracket_depth;
    // Generics (runtime/lang_generic.c).
    size_t generics_count;
    size_t instances_count;
    size_t instances_emitted;                             // the instances before this one have their code written
    uint32_t cap_frames;                                  // calls being read: their tokens are kept, not written
    SmithLangCallFrame calls[smith_lang_call_frames_max]; // the calls being read, the innermost last
    size_t call_top;
    size_t call_site_count;
    SmithLangCallSite call_sites[smith_lang_call_sites_max]; // the calls as the file wrote them, for the editor
    bool call_failed;                                        // a call nested too deep: the outermost is dropped
    bool recording;                                          // a generic's body is being read into smith_lang_gt
    uint32_t recording_generic;                              // its index
    bool recording_skip;  // its head had an error: the body is read to its end and dropped
    uint32_t skip_mark;   // where in smith_lang_gt the dropped body begins
    bool generic_next;    // the declaration about to begin is generic
    bool generics_loaded; // the generics of the imports are read
    SmithLangText generic_modules[smith_lang_generic_modules_max]; // the modules read for their generics
    size_t generic_modules_count;
    bool gen_dump_on; // SMITH_LANG_GENERICS names a file for what the generics write (a test's view of it)
    size_t gen_dump_used;
    int gen_dump_braces;
    int gen_dump_parens;
    char gen_prev;
    bool namespace_done;       // smith_lang_namespace wrote a generic call itself: there is no name to write
    uint32_t instance_depth;   // of the instance being written (0: the file's own code)
    uint32_t instance_current; // 1 + the instance being written (0: none)
    char prev_name[64];        // the last token, if it was a plain name: it may be a type the parser knows
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
    bool imports_watched; // the host watches the names the imported headers declared
    bool body_sparse;     // inside a function body the host offers only braces and watched names
    bool in_function;     // inside a function body: nothing declared here is the module's
    char params[smith_lang_params_max][smith_lang_param_max]; // the parameters of the function being declared whose
    uint32_t params_count;                                    // names are the module's: kept as written, not prefixed
    bool agg_pending;                                         // struct, union or enum seen, `{` not yet
    bool enum_pending;
    bool init_top;             // an initializer at file scope is being read
    bool tail_declarator;      // after an aggregate's `}` or a comma: names are declarators
    uint32_t typedef_depth;    // the brace depth the typedef statement began at
    bool in_attribute;         // inside [[ ]]: gnu::unused is no module path
    uint32_t attribute_parens; // inside the parentheses of __attribute__(( )): format(printf, 1, 2)
    bool attribute_next;       // __attribute__ was the last token: its parenthesis opens the arguments
    bool statement_typedef;    // `typedef` seen in this statement
    uint8_t own_words;         // module, import or export, typedef'd by this file: they are names, not keywords
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
    SmithToken recent[4]; // the tokens of this batch already lowered, newest first (the sparse path)
    size_t recent_count;
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
    SmithLangWritten written[smith_lang_written_max];
    size_t written_count;
    uint32_t written_slots[smith_lang_written_slots];
    SmithLangText module_text; // this module's name, as stored text
    bool qualifier_set;        // the cursor of a completion came right after `x::`
    bool qualifier_is_c;
    uint32_t qualifier_location;
    SmithLangText qualifier_binding;
    SmithLangText qualifier_prefix;
    SmithLangText qualifier_module;
    SmithLangText qualifier_partial;
    size_t chars_used;
    // Generated header and dump.
    size_t imports_count;
    SmithLangText smith_imports[smith_lang_imports_max];
    SmithLangText c_imports[smith_lang_imports_max];
    bool c_import_angled[smith_lang_imports_max];
    size_t c_imports_count;
    bool export_active;
    bool emit_synthetic;   // everything emitted is a copy with no token of its own in the source (a prototype)
    bool aligned;          // the header body keeps the source's lines (an interface read in memory, runtime/lang_lex.c)
    size_t aligned_line;   // the line of the header body that is being written (from 1)
    size_t aligned_column; // the column of the next character on it (from 1)
    size_t header_used;
    bool dump_enabled;
    size_t dump_used;
    bool dump_bol;
    uint32_t allowed[smith_lang_allowed_max];
    size_t allowed_count;
    size_t allowed_next;
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

[[gnu::always_inline]] static inline bool smith_lang_equals(const char *text, size_t length, const char *literal) {
    size_t expected = strlen(literal);
    return length == expected && memcmp(text, literal, expected) == 0;
}

[[gnu::always_inline]] static inline bool smith_lang_is(const SmithToken *token, SmithTokenKind kind,
                                                        const char *literal) {
    return token->kind == kind && smith_lang_equals(token->text, token->length, literal);
}

[[gnu::always_inline]] static inline bool smith_lang_punct(const SmithToken *token, const char *literal) {
    return smith_lang_is(token, SmithTokenKind_Punctuator, literal);
}

[[gnu::always_inline]] static inline bool smith_lang_ident(const SmithToken *token, const char *literal) {
    return smith_lang_is(token, SmithTokenKind_Identifier, literal);
}

// The table is sorted, so the words that start with one character are together: [first, last) per character, made once.
static uint8_t smith_lang_word_first[256];
static uint8_t smith_lang_word_last[256];

static void smith_lang_word_buckets(void) {
    if (smith_lang_word_last[(uint8_t)'w'] != 0) return; // "while" is the last entry: the table is made
    size_t count = sizeof smith_lang_words / sizeof smith_lang_words[0];
    for (size_t i = 0; i < count; i++) { // the table
        uint8_t c = (uint8_t)smith_lang_words[i].text[0];
        if (smith_lang_word_last[c] == 0) smith_lang_word_first[c] = (uint8_t)i;
        smith_lang_word_last[c] = (uint8_t)(i + 1);
    }
}

static SmithLangWord smith_lang_word(const char *text, size_t length) {
    if (length == 0) return SmithLangWord_Other;
    uint8_t c = (uint8_t)text[0];
    for (size_t i = smith_lang_word_first[c]; i < smith_lang_word_last[c]; i++) { // the words that start with c
        const SmithLangWordEntry *entry = &smith_lang_words[i];
        if (entry->length == length && memcmp(text + 1, entry->text + 1, length - 1) == 0) return entry->word;
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

// The comments and conditional directives a module's header keeps.
#include "lang_trivia.c"

// Names, bindings and the lowering of one identifier.
#include "lang_decl.c"

// The names as the source writes them.
#include "lang_names.c"

// The module line and the imports.
#include "lang_import.c"

// The head of a file-scope declaration.
#include "lang_head.c"

// Generic functions: `type T` parameters, their calls and their instances.
#include "lang_generic.c"

// A file that is not a module, with the host offering few tokens.
#include "lang_sparse.c"

// A module's private declarations, read as they stream by.
#include "lang_declare.c"

// Bodies, the start and end of the file, and the entry points.
#include "lang_stream.c"

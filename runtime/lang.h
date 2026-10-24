// The lowering of .smith files, and the interface between it and the program that gives it tokens.
//
// A .smith file is C with `module`, `import` and `export`, and sized type names (knowledge/smith-language.md). The
// lowering is a state machine over the file's tokens: it is given one token at a time and answers with the C tokens
// that replace it. It knows nothing of clang. A host feeds it: the clang hook in shim/hook.cpp (tokens after macro
// expansion, with questions to the parser), or the plain lexer of runtime/lang_lex.c (a header written without
// compiling the module).
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SmithTokenKind_Eof,
    SmithTokenKind_Identifier, // names and keywords alike: the lowering has its own keyword table
    SmithTokenKind_Number,
    SmithTokenKind_String,
    SmithTokenKind_Character,
    SmithTokenKind_Punctuator,
} SmithTokenKind;

constexpr uint32_t smith_token_no_handle = UINT32_MAX;

// What lies between tokens that the plain lexer (runtime/lang_lex.c) keeps for a module's generated header: comments
// and conditional directives. They are one list for the whole file, in source order, owned by the host for as long as
// the lowering runs; a token's `trivia` is the first entry before it that no earlier token had, and the entries before
// a token are those from there while their `location` is below the token's. A host that runs the compiler's
// preprocessor offers none: it sees the tokens of one branch.
typedef enum {
    SmithTrivia_Leading,  // a comment that is the first thing on its line (or follows another comment)
    SmithTrivia_Trailing, // a comment on the line of the token before it
    SmithTrivia_Open,     // #if, #ifdef or #ifndef
    SmithTrivia_Branch,   // #elif, #elifdef, #elifndef or #else
    SmithTrivia_Close,    // #endif
    SmithTrivia_Include,  // #include "h" or #include <h>: the header's name is the text, `angled` says which
} SmithTriviaKind;

typedef struct SmithTrivia {
    SmithTriviaKind kind;
    bool zero;        // Open: `#if 0`, whose text the lexer skips
    bool blank_after; // a comment followed by a blank line (before the next comment, directive or token)
    bool angled;      // Include: <h>, not "h"
    uint32_t location;
    uint32_t length;
    const char *text; // the comment, or the directive line from the `#`
    const struct SmithTrivia *next;
} SmithTrivia;

// One token. `text` is valid until the host is next asked for a token or the call to smith_lang_feed returns,
// whichever is first; the lowering copies what it keeps.
typedef struct {
    SmithTokenKind kind;
    bool line_start;   // the first token on its line
    bool space_before; // whitespace before it
    bool foreign;      // given by `next`: not spelled in the file being lowered (a header the stream entered)
    bool watched;      // the host watches the name (set by a sparse host; a host that offers every token sets none)
    bool declared;     // the parser has a declaration with this name (a typedef of u32, say, makes it no sized type)
    bool expand;       // emitted: macro-expand it when it re-enters the stream (the X of `c.X`)
    bool synthetic;    // emitted: it has no counterpart in the source (a `static`, a symbol label, a prototype's copy)
    bool copy;         // emitted: part of a copy of a declaration's head (the prototype of a definition), all of it
    bool replay;       // queued: part of an imported function that runs in the compiler (type values only; issue #107)
    bool tuple_brace;  // emitted: the `{` or `}` of a tuple\'s definition (a head holds it like a parenthesis)
    bool name;         // emitted: the name a declaration introduces
    uint32_t instance; // emitted: the generic instance whose body the token belongs to (0: none)
    uint32_t location; // the host's source location, kept by every token derived from this one
    uint32_t handle;   // the host's own token, to re-emit unchanged (smith_token_no_handle: make one from the text)
    uint32_t length;
    uint16_t
        via; // emitted: 1 + the import binding whose `x::` this name was written with (0: none); the header's includes
    const char *text;
    const char *header_text; // emitted: the spelling in a generated header, when it differs (uint64_t for u64)
    uint32_t header_length;
    const SmithTrivia *trivia; // given by `next`: comments and directives before the token
    bool completion;           // given by `next`: the place of an editor's cursor, where a name is being completed
    const char *partial;       // of a completion: what was typed of the name, before the cursor
    uint32_t partial_length;
} SmithToken;

// What the parser says a name means where the token is. Only what has been completely parsed is visible.
typedef enum {
    SmithLookup_None,
    SmithLookup_Type,  // a typedef name or a builtin
    SmithLookup_Value, // a variable, a function or an enumeration constant
    SmithLookup_Tag,
} SmithLookupKind;

typedef struct {
    SmithLookupKind kind;
    bool in_main_file; // declared in the file being lowered (a local, a parameter or a file-scope name)
    bool from_import;  // declared in a header that `import <h>` brought in (not one that `#include` brought in)
} SmithLookup;

// What the parser knows of a complete type: the questions reflection will ask (size, alignment, fields).
typedef struct {
    uint64_t size;      // in bytes
    uint64_t alignment; // in bytes
    uint32_t fields;    // of a struct or union; 0 for any other type
    bool is_record;
} SmithTypeInfo;

// What the parser says of a type written as tokens (`const char *`, `geometry_Point`): the questions a generic's
// `if constexpr` asks (is_integer(T), sizeof(T)) and the identity of a type for its instances.
typedef struct {
    bool complete; // size and alignment are known
    bool is_integer, is_float, is_signed, is_pointer, is_bool, is_enum, is_record, is_array;
    uint64_t size;
    uint64_t alignment;
    const char *canonical; // the type with every typedef looked through; valid until the next question
    uint32_t canonical_length;
    const char *pointee; // of a pointer: the type it points to, canonical; valid until the next question
    uint32_t pointee_length;
} SmithTypeQuery;

// A field of a struct, for `fields(T)`: what the parser says of the type written by a spelling. `status` is Ok when the
// type is a struct whose members are all named; `count` is then the number of named fields, and the field asked for
// (when its index is below `count`) has its name, the spelling of its type and whether it is a bit-field.
typedef enum {
    SmithFields_Ok,
    SmithFields_NotRecord,
    SmithFields_Union,
    SmithFields_Anonymous, // a member that is an anonymous struct or union
    SmithFields_Incomplete,
    SmithFields_Unreadable, // the spelling is no type
} SmithFieldsStatus;

typedef struct {
    SmithFieldsStatus status;
    uint32_t count;
    bool bit_field;
    bool has_slice; // the struct holds a run-time array, however deep (anonymous members, arrays and structs inside)
    bool tuple;     // the struct is a tuple: its members are `_0`, `_1`, and `fields` names them "0", "1"
    char name[128];
    char type[256];
} SmithFieldInfo;

// The type of an expression, for a generic whose type is not written at the call (`test::check_equal(a, b)`).
typedef struct {
    char written[256];   // as the parser's types are written, typedef names kept (`Point`), without qualifiers
    char canonical[256]; // with every typedef looked through: the identity of the type
    const char *why;     // when the host cannot say: what it could not understand (valid until the next question)
    bool explain;        // in: when the host cannot type it, `why` is the compiler's own message
    // When the compiler folds the expression to a number: an integer is `magnitude` (negated if `negative`, and
    // `overflow` when it needs more than 64 bits), a float is `real`.
    bool constant;
    bool is_float;
    bool negative;
    bool overflow;
    bool is_function; // the expression is a function (a designator), before it becomes a pointer to one
    bool is_array;    // the expression is an array of known size (the types here are those it decays to: a pointer)
    uint64_t array_size;
    uint32_t array_rank; // an array: its dimensions (`float[4][2]` is 2), `array_dims` the sizes (at most 8)
    uint64_t array_dims[8];
    char array_element[256]; // an array: its element type, every dimension stripped (`float`)
    uint64_t magnitude;
    double real;
} SmithExprType;

// The value of a C expression the compiler evaluated (`value_query`): a tree whose nodes the caller provides.
typedef enum {
    SmithValue_Int,
    SmithValue_Float,
    SmithValue_Struct, // a struct or a tuple: `items` are its fields in order
    SmithValue_Array,  // a string is an array of `char`
} SmithValueKind;

typedef struct SmithValue {
    SmithValueKind kind;
    bool is_signed;     // an integer: its signedness
    bool negative;      // an integer: the value is below zero (`magnitude` is its absolute value)
    bool is_f32;        // a float: `f32` (else `f64`)
    uint32_t bits;      // an integer: its width in bits (at most 64)
    uint64_t magnitude; // an integer
    double real;        // a float (an `f32` is exact in it)
    uint32_t count;     // a struct's fields, an array's elements
    struct SmithValue *items;
} SmithValue;

// A value query: the caller gives `nodes` (`capacity` of them, `used` is how many the host took); the host fills
// `root` and the tree below it from them. When it cannot, `why` is the compiler's message (valid until the next call).
typedef struct {
    SmithValue *nodes;
    uint32_t capacity;
    uint32_t used;
    SmithValue root;
    const char *why;
} SmithValueQuery;

// What a sparse host offers besides the watched identifiers and the end of the file.
typedef enum {
    SmithWatch_Names,  // nothing else
    SmithWatch_Braces, // the braces
    SmithWatch_All,    // every token
} SmithWatch;

// The facts of the target that `target::` names ask the compiler (module `target`, knowledge/smith-target-facts.md).
// The values of `arch` and `os` are SmithTargetArch_* and SmithTargetOs_*; a boolean fact is 0 or 1.
typedef enum {
    SmithTarget_Arch,
    SmithTarget_Os,
    SmithTarget_AddressSanitizer,
    SmithTarget_UndefinedSanitizer,
    SmithTarget_ThreadSanitizer,
    SmithTarget_Assertions,
    SmithTarget_FloatRadix,
    SmithTarget_F32MantissaBits,
    SmithTarget_F64MantissaBits,
    SmithTarget_FloatEvalMethod,
    SmithTarget_StaticAnalyzer,
} SmithTarget;

typedef struct {
    void *context;
    // The next token, after the last one given. `unexpanded` asks that macros are not expanded (the X of `c.X`).
    void (*next)(void *context, SmithToken *token, bool unexpanded);
    // One token of the output. It goes to the parser in order; the lowering hands over everything it read.
    void (*emit)(void *context, const SmithToken *token);
    // Include a header, as `#include <h>` or `#include "h"` would, at this point of the output.
    void (*include)(void *context, const char *header, bool angled);
    void (*error)(void *context, uint32_t location, const char *message);
    // Questions. A host without a parser answers SmithLookup_None and `false`.
    SmithLookup (*lookup)(void *context, const char *name, uint32_t length, SmithLookupKind space);
    // The size, alignment and field count of a complete type, named as a typedef or (for SmithLookup_Tag) as a tag.
    bool (*type_info)(void *context, const char *name, uint32_t length, SmithLookupKind space, SmithTypeInfo *info);
    // A type written as tokens: false when the host cannot read it (then the spelling itself identifies it).
    bool (*type_query)(void *context, const char *spelling, uint32_t length, SmithTypeQuery *out);
    // The type of an expression written as tokens separated by \x1f (names, fields, `*`, `&`, `[]`, casts, literals,
    // calls, anything an expression is), read by the compiler in the scope the parser is in. False with `out->why` when
    // it cannot.
    bool (*expr_type)(void *context, const char *tokens, uint32_t length, SmithExprType *out);
    // The value of a C expression written as tokens separated by \x1f (as `expr_type`), evaluated by the compiler in
    // the scope the parser is in: an integer, a float, a struct or an array of them. False with `out->why` when it is
    // not a constant, or is a pointer (C23: a `constexpr` pointer must be null). Null for a host without a parser: the
    // raw lowering reports that the expression needs smith's compiler.
    bool (*value_query)(void *context, const char *tokens, uint32_t length, SmithValueQuery *out);
    // The type of the function being parsed, as the compiler prints it with typedefs looked through (`return {1, 2.0f}`
    // in a function that returns a tuple): false outside a function or for a host without a parser.
    bool (*return_type)(void *context, char *out, uint32_t capacity);
    // A generic instance is about to be emitted (`id` is in the tokens that follow): the instance whose body asked for
    // it (0: the file's own code), the call that did, how to name it in a note, and the place of the generic's name.
    void (*instance)(void *context, uint32_t id, uint32_t parent, uint32_t call_location, const char *description,
                     uint32_t definition_location);
    // A host location outside the file being lowered, for the tokens of an import's function over `type` that are
    // written here when their own place in the module's source cannot be found: no check of the file's own code reads
    // them.
    uint32_t (*types_place)(void *context);
    // The exported functions of the imports that the file runs at compile time (issue #107, runtime/lang_body.c). A
    // host with a parser leaves all three null when it cannot offer them. `mentions`: whether the file's text names the
    // function where C requires a constant (a `static_assert`, a `constexpr` initializer, an `if constexpr`, a
    // file-scope initializer or head). `read_body`: the `<module>.body` of the module whose header is `header` (a/b.h),
    // made if it is missing or older than the module's source; its text and the path to include it by (null: made in
    // memory). `include_text`: puts text in front of the parser as an included file, defined once, before the file's
    // first declaration.
    bool (*mentions)(void *context, const char *name, uint32_t length);
    bool (*read_body)(void *context, const char *header, const char **text, uint32_t *length, const char **path);
    void (*include_text)(void *context, const char *text, uint32_t length);
    // The host location of a line and column (from 1) of a file, or UINT32_MAX when it cannot say.
    uint32_t (*locate)(void *context, const char *file, uint32_t line, uint32_t column);
    // The value of a fact of the target, from the compiler's preprocessor (`defined(__aarch64__)`,
    // `__has_feature(...)`,
    // `__FLT_MANT_DIG__`). A host without a preprocessor leaves this null: the lowering then writes the equivalent
    // `#if` or macro into its output instead (the raw lowering, `smith cc --smith-lower` and `--smith-header`).
    bool (*target_fact)(void *context, SmithTarget fact, int64_t *value);
    // The value of a `constexpr` constant of integer type the parser has completely parsed (a variable of the file, or
    // a local declared by an earlier statement), named as the compiler sees it. Null for a host without a parser.
    bool (*constant)(void *context, const char *name, uint32_t length, int64_t *value);
    // The `index`th named field of the struct a spelling names (`fields(T)`), and how many there are. False when the
    // host has no parser.
    bool (*type_field)(void *context, const char *spelling, uint32_t length, uint32_t index, SmithFieldInfo *out);
    // The `index`th field of a struct or union: its name and the spelling of its type, valid until the next call.
    bool (*field)(void *context, const char *name, uint32_t length, SmithLookupKind space, uint32_t index,
                  const char **field_name, const char **field_type);
    // Whether a file is found by the include paths; if so its text, in a buffer the host keeps until the next call.
    bool (*read_header)(void *context, const char *relative, const char **text, uint32_t *length);
    // A sparse host offers only the identifiers it was told to watch, and the end of the file, until told otherwise
    // (a module's declarations need every token, its function bodies the braces). `previous` is its record of the last
    // few tokens, as the parser saw them.
    void (*watch)(void *context, const char *name, uint32_t length);
    void (*watch_mode)(void *context, SmithWatch mode);
    bool (*previous)(void *context, unsigned back, SmithToken *token);
    // The module declared a name: the host may rename its uses itself (the prefixed spelling is given).
    void (*rename)(void *context, const char *name, uint32_t length, const char *prefixed, uint32_t prefixed_length);
    // Whether the file's text may declare `main` (it contains the word); false lets a declaration be decided at its
    // first token.
    bool maybe_main;
    // Whether the file's text may declare a generic (it contains `type `); false lets a declaration be decided at its
    // first token.
    bool maybe_generic;
    // Whether the file's text may contain an `if constexpr` (the words in that order): the host then offers every token
    // of a function body, because `if` and `else` are keywords and a sparse host is offered only identifiers.
    bool maybe_if_constexpr;
    // Whether the file's text may contain a tuple type (`struct {` and a list with no `;` before the `}`): the host
    // then offers every token, because `struct` is a keyword and a tuple's uses (`t[0]`, `countof(t)`) are found among
    // them.
    bool maybe_tuple;
    // Whether the file's text may declare something of type `type` (runtime/lang_type.c): the host then offers every
    // token, because a type value is found by the tokens around a type name.
    bool maybe_type_value;
    // How many `import` lines the text has (an import's functions over `type` are defined after the last, issue #107).
    uint32_t import_lines;
    // An import's header says it has functions over `type` (`// smith-types:`): the host sets maybe_type_value from now
    // on, as if the text declared a type value. May be null.
    void (*type_values)(void *context);
    // Whether the file's text uses `fields(` or the `Field` type (runtime/lang_fields.c): `Field` is defined at the
    // file's first declaration. The host also sets maybe_type_value, which offers every token.
    bool maybe_field;
    bool maybe_slice; // the text has a run-time array (`T xs[:]`, `xs[a:b]`): every token is offered, none by the hook
                      // alone
    // Watch every name the imported C headers declared (so that a bare use of one in a module is found).
    void (*watch_imports)(void *context);
    // Whether the file being lowered has declared anything yet (an import must come first).
    bool (*declared)(void *context);
    const char *main_file; // the path of the file being lowered
    // The lowering is read for the module's declarations only (the header pass, which has no compiler): what a function
    // body would need a compiler for, such as the type of a generic call's arguments, is skipped without an error.
    bool declarations_only;
    // After read_header said no: the whole message for the import (the host looked for the module's source too and
    // knows what it tried), or null for the default one.
    const char *(*read_problem)(void *context);
    // A name of a module that could not be imported is used here: the host keeps the diagnostics at that location out
    // of the way (the missing module is the one error).
    void (*suppress)(void *context, uint32_t location);
    // A note for the error the compiler reports at this place (a name it will not find): the lowering knows why, and
    // the compiler does not. A host without diagnostics leaves this null.
    void (*hint)(void *context, uint32_t location, const char *message);
    // Which `import <h>;` or `import "h";` declared `name` (a declaration of that space, else a macro): the header as
    // that import spells it, `<stdio.h>` or `"mine.h"`, never an internal header the compiler found the name in. Null
    // when no import did. Asked by SMITH_LANG_ASK_IMPORTS; a host without a parser leaves it null.
    const char *(*declared_by)(void *context, const char *name, uint32_t length, SmithLookupKind space);
} SmithHost;

typedef struct SmithLang SmithLang;

// What a host's fast path does with a token, decided by the lowering without any lookahead.
typedef enum {
    SmithLangAct_Slow,   // not decided: give the token to smith_lang_feed (nothing was changed)
    SmithLangAct_Keep,   // goes on as it is
    SmithLangAct_Rename, // goes on as `text`
    SmithLangAct_Sized,  // becomes the sized type `text` (the compiler's own name for it)
    SmithLangAct_Drop,   // is dropped (a second `static`)
    SmithLangAct_Done,   // inside the lowering: already written out
} SmithLangActKind;

typedef struct {
    SmithLangActKind kind;
    bool insert_static; // a `static` goes before the token
    uint32_t length;
    const char *text;
} SmithLangAct;

// The fast path for a token: see SmithLangAct. Only a sparse host (one with `previous`) may call it.
SmithLangAct smith_lang_fast(SmithLang *lang, const SmithHost *host, const SmithToken *token);

SmithLang *smith_lang_new(void);
// Tells a sparse host the names to offer from the start: module, import, export and the sized types.
void smith_lang_watch_basics(const SmithHost *host);
// Gives the lowering one token. Returns true when the host must drop it and use what was emitted instead.
bool smith_lang_feed(SmithLang *lang, const SmithHost *host, const SmithToken *token);
// Whether a macro of an imported header may be expanded at this location (it was named c.X there).
bool smith_lang_c_use_allowed(const SmithLang *lang, uint32_t location);
bool smith_lang_is_module(const SmithLang *lang);
// Whether the file imports smith::io, so that its `i32 main(io::Io *io)` is rewritten: the host reads `i32` whole.
bool smith_lang_io_import(const SmithLang *lang);
// In a module's function body, whether the host offers only braces and the names it watches (the host's fast paths).
bool smith_lang_body_sparse(const SmithLang *lang);
// Whether the file has run-time arrays from an import (the host then offers every token and keeps none).
bool smith_lang_slices_on(const SmithLang *lang);
// Whether `name` is a parameter of the function whose body is being read, and so is no name of the module even when
// the module exports one by that name (the host must leave it as written).
bool smith_lang_param_name(const SmithLang *lang, const char *name, size_t length);
// Whether `name` is the name of an import (its last part, or its alias): where it is a name of the module too, the
// host must let the lowering see it, since `x::` is the import's.
bool smith_lang_import_name(SmithLang *lang, const char *name, size_t length);
// Whether `name` is a typedef the module declares (the lowering's own table, no question to the parser): a keyword that
// follows it is used as a name.
bool smith_lang_type_name(SmithLang *lang, const char *name, size_t length);
// A brace of such a body that changes nothing but the nesting: done, and the token goes on unchanged. False for the
// function's own closing brace and for a struct, union or enum, which the full path reads.
bool smith_lang_fast_brace(SmithLang *lang, const SmithHost *host, bool open);
// Whether tokens are held for a later call: the host keeps the tokens it handed over (their handles) until it is false.
bool smith_lang_holds(const SmithLang *lang);
unsigned smith_lang_errors(const SmithLang *lang);
// The generated C header of the module (empty if the file is no module), after the whole file was fed.
const char *smith_lang_header(const SmithLang *lang, size_t *length);
// The lowered text of the file, when SMITH_LANG_DUMP names a file to write it to.
void smith_lang_finish(SmithLang *lang);

// The interface of a module read from its source, in memory, for a host that finds a module by its source instead of
// its header. `file` names the source (for the module line's check and for messages) and `text` is its text. It is
// safe to call from inside a lowering (it keeps that lowering's state and gives it back). Where the header
// `smith cc --smith-header` writes is one file, the interface is three texts:
//   - `text`, the declarations (at least smith_lang_interface_capacity() bytes), each on the line the module has it
//     on (the first line is `#pragma once`, and no declaration is on it), so that a diagnostic that points into it is
//     at the right line of the module;
//   - `includes`, the `#include` lines they need, which the host enters in front of them;
//   - `meta`, the header's comment lines `// smith-module:`, `// smith-imports:` and `// smith-names:`, which is
//     all the lowering of an importer reads of a header besides the declarations (read_header gives it these).
// Returns false, with the first error (`line:column: message`) in `error`, when the source has one.
typedef struct {
    char *text;
    size_t text_capacity;
    size_t text_used;
    char *includes;
    size_t includes_capacity;
    size_t includes_used;
    char *meta;
    size_t meta_capacity;
    size_t meta_used;
} SmithInterface;

size_t smith_lang_interface_capacity(void);
bool smith_lang_interface(const char *file, const char *text, size_t length, SmithInterface *result, char *error,
                          size_t error_capacity);

// Where a module's source declares its exports: a line "name line column" (from 1, the name as written) for each, in
// the order declared, read the way smith_lang_interface reads it. Returns the bytes written to `out`.
size_t smith_lang_declarations(const char *file, const char *text, size_t length, char *out, size_t capacity);
// The module of the file being lowered ("a::b"), or null if it is no module.
const char *smith_lang_module_name(const SmithLang *lang);

// A C keyword that follows a complete type (a sized type name) and cannot continue its specifiers is the program's name
// for something: `i32 double(i32 x)`. Hosts that read the text report it themselves, once, at the name, with
// smith_lang_keyword_message; what it names is read from the text around it (null: it is no name). Plain functions of
// text, with no state.
bool smith_lang_misused_keyword(const char *text, size_t length);
const char *smith_lang_keyword_name_use(const char *text, size_t size, size_t start, size_t end);
size_t smith_lang_keyword_message(char *out, size_t capacity, const char *keyword, size_t length, const char *noun);

// The command of a compile that names .smith inputs, made what `smith cc` makes it (runtime/lang_command.c):
// `-std=gnu2y` unless the command names a standard (or has a C++ input), and `-x c` in front of each .smith input
// (before the `--` when the inputs follow one, as clangd's commands have them). Nothing is changed when the command
// names a language itself (`-x`), is clang's `-cc1` form, or has no .smith input. `argv[0 .. first)` are copied as they
// are. Writes the new arguments to `out`, a null after them, `capacity` entries at most; returns their count, 0 when
// the command stays as it is, or -1 when `out` is too small. `smith cc` and `smith lsp` (shim/clangd.cpp, for the
// commands clangd compiles with) apply this one rule.
int smith_lang_command(int argc, char **argv, int first, char **out, size_t capacity);

// The names as the source writes them (runtime/lang_names.c). For each name the lowering made, the one the compiler
// sees (`geometry_box_Box`), the one written in this file (`box::Box`, or `Box` in the module itself), its member
// (`Box`) and its module (`geometry::box`). The text is valid until the next smith_lang_new; a host that keeps it
// copies it. `direct` is false for a name of a module the file reaches only through another's imports (it is written
// with its whole module name then).
typedef struct {
    const char *lowered;
    uint32_t lowered_length;
    const char *written;
    uint32_t written_length;
    const char *member;
    uint32_t member_length;
    const char *module;
    uint32_t module_length;
    bool direct;
} SmithWrittenName;

// A completion whose cursor came right after `x::` (the host gave the lowering a token with `completion` set there):
// the lowering took `x::` away and left the cursor, so that the parser completes a name; this says which `x`.
typedef struct {
    const char *binding; // box
    const char *prefix;  // geometry_box_ (empty for c)
    const char *module;  // geometry::box (empty for c)
    const char *partial; // what was typed after the `::`
    uint32_t partial_length;
    uint32_t location; // of the cursor
    bool is_c;
} SmithQualifier;

bool smith_lang_qualifier(const SmithLang *lang, SmithQualifier *out);

// A call to a generic as the file wrote it, for an editor: where the name is, what to show on hover, and where the
// generic is written (its file and name; `location` is the host's own place when the generic is in this file, else 0).
typedef struct {
    uint32_t location;
    uint32_t length;
    char hover[1024];
    char file[512];
    uint32_t definition_location; // the host's place of the generic's name (UINT32_MAX: use line and column)
    uint32_t line;                // from 1, in `file`
    uint32_t column;
} SmithCallSite;

size_t smith_lang_call_site_count(const SmithLang *lang);
bool smith_lang_call_site_at(const SmithLang *lang, size_t index, SmithCallSite *out);

// The instances the file made: the compiler's name of each and the source of the generic it came from (an instance is
// compiled in the importer's file, but it is its generic's module's code: the checks that judge a module by its file
// ask this).
typedef struct {
    char name[256];
    char source[512];
    bool exported; // the generic is an export of its module (else a private helper: its instances are no export)
} SmithInstanceOrigin;

size_t smith_lang_instance_count(const SmithLang *lang);
bool smith_lang_instance_at(const SmithLang *lang, size_t index, SmithInstanceOrigin *out);

size_t smith_lang_written_count(const SmithLang *lang);
bool smith_lang_written_at(const SmithLang *lang, size_t index, SmithWrittenName *out);
bool smith_lang_written_find(const SmithLang *lang, const char *lowered, size_t length, SmithWrittenName *out);

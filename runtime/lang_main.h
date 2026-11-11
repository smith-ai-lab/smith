// Generated split of smith.c (#2685): what runtime/lang.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"

constexpr size_t smith_lang_names_max = 65536;

constexpr size_t smith_lang_due_max = 32; // tags one declaration defines, each with a typedef to come

// The module's private declarations the header pass keeps, for the instance section of its header (lang_private.c).
constexpr size_t smith_lang_priv_text_max = (size_t)1 << 19;

constexpr size_t smith_lang_priv_ranges_max = 1024;

constexpr size_t smith_lang_priv_names_max = 2048;

constexpr size_t smith_lang_priv_notes_max = 4096;

constexpr size_t smith_lang_gen_uses_max = 8192;

constexpr size_t smith_lang_hidden_max = 1024; // private names of imports an instance section declares

constexpr size_t smith_lang_linked_max = 256; // private names of a module that have a symbol (its header says which)

constexpr size_t smith_lang_slots = 131072; // a power of two, twice the names

constexpr size_t smith_lang_chars_max = (size_t)1 << 20;

constexpr size_t smith_lang_scratch_max = (size_t)1 << 18;

constexpr size_t smith_lang_head_max = 4096;

constexpr size_t smith_lang_queue_max = 1024;

constexpr size_t smith_lang_batch_max = 4096;

constexpr size_t smith_lang_call_sites_max = 2048; // calls to generics the editor can be told of

constexpr size_t smith_lang_call_frames_max = 16; // generic calls inside generic calls' arguments

constexpr size_t smith_lang_braces_max = 256;

constexpr size_t smith_lang_bindings_max = 128;

constexpr size_t smith_lang_imports_max = 128;

constexpr size_t smith_lang_header_max = (size_t)1 << 20;

constexpr size_t smith_lang_dump_max = (size_t)1 << 22;

constexpr size_t smith_lang_allowed_max = 16; // the macro names c::X lets expand, until they have

constexpr size_t smith_lang_name_max = 256;

constexpr size_t smith_lang_params_max = 16;

constexpr size_t smith_lang_param_max = 64;

constexpr size_t smith_lang_array_params_max = 8;

constexpr size_t smith_lang_array_dims_max = 3;

constexpr size_t smith_lang_array_tokens_max = 4;

constexpr size_t smith_lang_array_text_max = 24;

// The size of an array parameter, as the tokens that wrote it (`countof(xs)` is that, in parentheses).
typedef struct {
    char name[smith_lang_param_max];
    uint32_t dims;
    struct {
        uint32_t count;
        bool whole; // the size fit: its tokens are all here
        SmithTokenKind kinds[smith_lang_array_tokens_max];
        char texts[smith_lang_array_tokens_max][smith_lang_array_text_max];
    } sizes[smith_lang_array_dims_max];
} SmithLangArrayParam;

constexpr size_t smith_lang_message_max = 512;

constexpr size_t smith_lang_cycle_max = 72;

constexpr size_t smith_lang_written_max = 8192;

constexpr size_t smith_lang_written_slots = 16384; // a power of two, twice the names

constexpr size_t smith_lang_transitive_max = 72;

constexpr size_t smith_lang_cond_depth_max = 64; // #if nested in a module the header pass reads

constexpr size_t smith_lang_include_max = 16384; // the header's include lines

constexpr size_t smith_lang_cimports_max = 64; // `import <h>;` lines of one module

constexpr size_t smith_lang_cuses_max = 512; // the C names (`c::X`) that exported declarations use

constexpr size_t smith_lang_cond_lines_max = 32; // lines of one chain: #if, #elif..., #else

constexpr size_t smith_lang_cnames_max = 16384; // exports of imported modules that have a C name of their own

constexpr size_t smith_lang_gt_max = (size_t)1 << 17; // tokens of generics, and of the calls being read

constexpr size_t smith_lang_gt_text_max = (size_t)1 << 21; // their text

constexpr size_t smith_lang_it_max = (size_t)1 << 15; // tokens of the types instances were asked for

constexpr size_t smith_lang_it_text_max = (size_t)1 << 19; // their text

constexpr size_t smith_lang_body_names_max = 512; // exported functions with a body that one module lists

constexpr size_t smith_lang_generics_max = 256; // generics known: the module's own and the imports'

constexpr size_t smith_lang_instances_max = 4096; // instances asked for in one file

constexpr size_t smith_lang_type_params_max = 32; // `type T` parameters of one generic

constexpr size_t smith_lang_call_params_max = 32; // parameters of one generic

constexpr size_t smith_lang_entries_max =
    smith_lang_type_params_max + smith_lang_call_params_max; // types of an instance

constexpr size_t smith_lang_instance_depth_max = 32; // generics asked for by the instances of generics

constexpr size_t smith_lang_generic_modules_max = 64; // modules whose generics were read

constexpr size_t smith_lang_rfor_depth_max = 16; // range `for` loops over slices open at once

constexpr size_t smith_lang_if_depth_max = 32; // `if constexpr` blocks open at once

constexpr size_t smith_lang_constants_max = 256; // `constexpr` constants a condition may name

constexpr size_t smith_lang_condition_max = 200; // tokens of one condition

constexpr size_t smith_lang_generic_scan_max = 200; // tokens read ahead to see whether a declaration is generic

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
    bool start_line; // the call as written is the first token on its line
    uint32_t count;  // smith_lang_gt.count and text_used when it began
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
    uint32_t location;          // of the declaration's name in the source, in the host's own measure
    SmithLangText c_name;       // the C name `[[std::c_name("...")]]` chose for an export (length 0: the prefixed name)
    uint16_t generic;           // 1 + the index of the generic function this name is (0: it is not one)
    bool needed;                // the body of an exported generic names it: it is in the header's instance section
    char tag_word;              // a tag: 's', 'u' or 'e' for struct, union or enum, or 0
    bool typedef_due;           // a tag whose typedef is not written yet: it goes before the next declaration
    SmithLangText tag_spelling; // the tag as C spells it when that is not the name's own (a payload enum's struct)
} SmithLangName;

// A private declaration, kept as the lowering wrote it (a function or a variable as the declaration that carries its
// symbol), with the names it uses from imports noted, to be written to the header if an exported generic needs it.
typedef struct {
    uint32_t from; // [from, to) of SmithLang::priv_text
    uint32_t to;
    uint32_t note_first;
    uint32_t note_count;
    uint8_t form; // a SmithLangForm
    bool linked;  // a function or a variable the module defines with a symbol (not a type or a constexpr)
    bool needed;
    bool done; // its text is complete
} SmithLangPrivRange;

typedef struct {
    SmithLangText name;  // as written
    SmithLangText cname; // as lowered
    uint32_t range;      // of SmithLang::priv_ranges
} SmithLangPrivName;

typedef struct {
    uint16_t via; // the binding the name was written through
    bool tag;
    SmithLangText name;
} SmithLangPrivNote;

// One token a generic keeps (a function with `type T` parameters, runtime/lang_generic.c), or of the calls being read
// and the types instances were asked for. 28 bytes; the text is in the pool's own.
typedef enum {
    SmithLangGen_Plain,
    SmithLangGen_CallBegin, // a call that may be to a generic: `module::member`, then the whole call, then CallEnd
    SmithLangGen_CallEnd,
    SmithLangGen_IfConstexpr,  // `if constexpr`: the condition and the branch that is kept are chosen for each instance
    SmithLangGen_ForConstexpr, // `for constexpr`: C's for, run for each instance and written out once per iteration
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
    uint32_t written; // 1 + the offset of the text of a call this token begins, as the source wrote it (0: none)
    uint32_t covers;  // with `written`: how many tokens, this one included, the lowered call took
} SmithLangGenToken;

// What a compile-time parameter of a generic is: `constexpr Type T`, `auto x`, `constexpr auto f` (a function known at
// compile time), `constexpr usize i` (a number known at compile time) or `auto... rest` (the last parameter: the types
// of the arguments that remain).
typedef enum {
    SmithLangSlot_Plain, // an ordinary parameter (only of a parameter, never of a slot)
    SmithLangSlot_Type,
    SmithLangSlot_Auto,
    SmithLangSlot_Function,
    SmithLangSlot_Pack,
    SmithLangSlot_Number, // `constexpr usize i`: an integer constant, one instance for each value
} SmithLangSlot;

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
    uint8_t type_kind[smith_lang_type_params_max];     // a SmithLangSlot
    uint8_t type_concept[smith_lang_type_params_max];  // of an `auto` parameter written `Num x`: 1 + the concept
    bool is_inline;
    bool exported;
    bool own;            // the module being lowered defines it (else an import's)
    bool needed;         // own and private: an exported generic calls it, so its record is in the header
    bool hidden;         // an import's private generic (its header holds the record for the instances of exported ones)
    bool plain;          // no compile-time parameter: an exported function over `type`, whose record importers define
    uint32_t note_first; // own, private: the names its body writes through imports, in SmithLang::priv_notes
    uint32_t note_count;
} SmithLangGeneric;

// A name an own generic's body writes (a private function, a type, another generic...), to find what an exported
// generic needs of the module's private code.
typedef struct {
    uint32_t generic;
    uint32_t name; // an index of SmithLang::names
} SmithLangGenUse;

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
    bool inc_emitted; // the lines so far are in the header's includes, and so are the rest as they come
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

// What the lowering of run-time arrays (runtime/lang_slice.c) keeps between tokens, with the rest of the lowering's
// state so that a module read aside keeps its own.
typedef struct {
    char armed[96]; // the name of a slice just declared, whose `=` may convert an array
    char armed_tag[512];
    char qualified[96]; // the name `m::T` was resolved to, at the place `qualified_at` (a declaration's type)
    uint32_t qualified_at;
    uint32_t brace_skip;  // the location of a `{` of a compound literal that was written again
    char typeof_tag[512]; // the slice a `typeof(T[:])` just wrote, which a declaration's `=` may convert to
    uint32_t skip;        // the location of the first token of a chain that was written again
    bool after_boundary;  // the token before the one being lowered opens or ends a statement
    char tags[32][512];   // the tags defined at file scope so far
    uint32_t tag_count;
} SmithLangSliceState;

typedef struct {
    SmithLangText name;
    SmithLangText prefix; // game_replay_
    SmithLangText module; // game::replay
    bool is_c;
    bool is_target; // `import target;`: the facts of the target, no module of any file (runtime/lang_target.c)
    bool is_self;   // the module's own last name (`tools::dup` in std::tools): an import of that name replaces it
    bool missing;   // the module could not be imported: its names are not checked, the import was the one error
    bool in_header; // an exported declaration (or a generic, or an inline body) of the module uses a name written x::
    bool listed;    // the module's header lists its exports (// smith-names:): a name it does not list is no export
} SmithLangBinding;

// A C name that an exported declaration uses, written `c::name` (`struct c::name` is a tag).
typedef struct {
    SmithLangText name;
    bool tag;
} SmithLangCUse;

// A token of a declaration head, held until the declaration is known.
typedef struct {
    SmithTokenKind kind;
    SmithLangRole role;
    bool line_start;
    bool space_before;
    bool expand;
    bool synthetic; // the lowering made it: it has no token of its own in the source (a tuple's tag, fields and
                    // semicolons)
    bool hoisted;   // part of a slice's struct that the code defines at file scope, before the head
    bool after_member;
    bool after_tag;
    char tag_word; // 's', 'u' or 'e' when after_tag
    bool typeish;
    char before;  // the first character of the token before, when that was a punctuator
    uint16_t via; // the binding the name was written through, 1 + its index (0: none)
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
    char last_tag_word;   // and its first letter
    bool checking;        // names in this declaration are checked for private types
    uint32_t declared;    // how many names the head declares
    uint32_t last_name;   // the held token of the last one
    bool prev_star;       // the token before was *
    bool prev_tag;        // the token before was struct, union or enum
    bool static_inserted; // `static` was written before the first token, so a later one is dropped
    bool prev_ident;      // the token before was a name (not a keyword)
    bool param_bracket;   // the token before was the `[` of an array parameter: its size follows
    bool attribute_first; // the declaration begins with [[ ]]: `static` goes after them, before the token that follows
    SmithLangText c_name; // [[std::c_name("...")]] before this export (length 0: none)
    SmithLangText enum_prefix; // an exported enum's C name when its tag is declared: its constants are `<it>_<Value>`
    uint32_t c_name_location;
    bool c_name_used;  // a name this declaration introduces took it
    bool generic;      // the head has a `type T` parameter: the declaration is recorded, not compiled
    bool plain;        // ... a function over `type` that has no such parameter (issue #107)
    bool result_call;  // a generic's result type is a call (`slice_of(T, k) name(...)`): its `)` ends the type
    bool linked;       // a private function or variable that has a symbol: an exported generic's instance calls it
    bool is_alias;     // `constexpr Type Y = X;`: a type alias, a name of the module's types like a typedef's
    bool alias_header; // an exported alias: the header gets `typedef X Y;`, written when the declaration ends
    size_t alias_mark; // the header's length after the `typedef` keyword written for it
    char alias_name[256];
} SmithLangDecl;

// An `if constexpr` outside a generic whose then-block is being read (runtime/lang_target.c).
typedef struct {
    int32_t target;  // the brace count before the then-block's `{`: the `}` that returns to it ends the block
    bool raw;        // the raw lowering: the branches are written between `#if`, `#else` and `#endif`
    bool in_else;    // raw: the last block is the `else`'s
    bool file_scope; // around declarations (issue #335): the branch's braces are dropped, they open no scope
} SmithLangIfFrame;

// A `constexpr` constant the raw lowering lets a condition name (a compile asks the parser).
typedef struct {
    SmithLangText name;
    SmithLangText text; // the raw lowering's `#if` text of the initializer
} SmithLangConstant;

// `try` and `errdefer` (runtime/lang_try.c): the flags of the open `errdefer`s and the statement being rewritten.
typedef struct {
    bool on;            // a `try` or `errdefer` was seen
    bool after_return;  // the token before is a `return` this lowering rewrote
    bool just_returned; // the same, for the token being lowered
    uint32_t next_id;
    uint32_t flag_count;
    struct {
        uint32_t id;
        uint32_t depth; // the brace depth of the block the `errdefer` is in
    } flags[16];
    bool ret_open; // a `return` is being read, to its `;`
    int32_t ret_parens;
    uint32_t ret_start; // the brace depth it began at
    uint32_t ret_id;
    int ret_kind;
    bool try_open; // a `try` is being read, to its `;`
    int32_t try_parens;
    uint32_t try_start;
    uint32_t try_id;
    uint32_t try_location;
    int try_kind;
    bool try_payload; // the `try` is a variant's payload in braces: it ends at the `,` or `}`
    bool try_wrap;    // the `try` is the whole value of a `return`: its value is returned as `.ok` or `.some`
    char type[256];   // the function's return type
    char ret_type[256];
} SmithLangTry;

// Lambdas (runtime/lang_lambda.c): the one being read, and how much waits for the end of the function.
typedef struct {
    uint32_t phase; // 1: the tokens of a lambda are being kept
    uint32_t start; // the place of its `[`
    uint32_t count; // tokens kept
    int32_t parens;
    int32_t braces;
    bool body;        // the body's `{` was read
    uint32_t pending; // tokens of definitions written when the function ends
    size_t text_used;
    bool started;     // a token of the file was seen
    uint32_t depth;   // the braces open, counted here (a file without a module line keeps no count)
    bool after_close; // the last one ended a declaration or a function
    SmithToken last;  // the last token seen (a name is `x`; `return` is itself)
} SmithLangLambda;

struct SmithLang {
    SmithLangTry tr;
    SmithLangLambda lambda;
    bool broken;
    bool advance_skip; // the token just lowered took its matching bracket with it: no call counts it
                       // (runtime/lang_tuple.c)
    SmithLangSliceState slice;
    bool field_import;  // an import's generics use `fields`: `Field` is defined at this file's first declaration
    bool field_defined; // `Field` (runtime/lang_fields.c) is defined in this file
    bool enums_seen;    // an enum with payloads was written in this file, or an import's header has one (lang_enum.c)
    bool slices_seen;   // a slice was written in this file, or an import's header has one (runtime/lang_slice.c)
    bool tuples_seen;   // a tuple type was written in this file, or an import's header has one (runtime/lang_tuple.c)
    bool broken_reported;
    char limit[160]; // the first table that filled, as an error says it (empty: none); reported once
    bool limit_reported;
    bool started;
    bool finished;
    const char *main_file;
    size_t header_full_used;
    size_t pending;               // the held identifier waiting for the token after it (SIZE_MAX: none)
    bool pending_export;          // `export` was read, and the declaration it applies to is next
    SmithLangText pending_c_name; // [[std::c_name("...")]] was read: the export after it has that C name
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
    bool export_tail;     // the header has the export's end, though export_active is off (a variable, a prototype)
    bool tail_open;       // an export just ended in the header: a comment after it on its line follows it there
    uint32_t nolint_open; // NOLINTBEGIN comments the header has no NOLINTEND for yet (a pair stays together)
    bool unnamed_c_name;  // an export ended that took no name from its [[std::c_name]]
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
    bool recording_skip;      // its head had an error: the body is read to its end and dropped
    uint32_t skip_mark;       // where in smith_lang_gt the dropped body begins
    bool generic_next;        // the declaration about to begin is generic
    bool generic_plain;       // ... a plain one: a function over `type` that is exported (issue #107)
    bool type_import;         // an import has functions over `type`: their records are defined after the last import
    bool generic_result_call; // ... and its result type is a call
    bool generics_loaded;     // the generics of the imports are read
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
    bool generated_name;  // the token being lowered is the lowering's own spelling: no check of C names (issue #203)
    bool body_sparse;     // inside a function body the host offers only braces and watched names
    bool in_function;     // inside a function body: nothing declared here is the module's
    char params[smith_lang_params_max][smith_lang_param_max]; // the parameters of the function being declared whose
    uint32_t params_count;                                    // names are the module's: kept as written, not prefixed
    SmithLangArrayParam array_params[smith_lang_array_params_max]; // the array parameters of that function
    uint32_t array_param_count;
    int32_t array_capture;                    // the dimension of an array parameter whose size is being read (-1: none)
    bool array_after_close;                   // the last token closed a dimension: a `[` continues the parameter
    bool countof_watched;                     // the host offers `countof`, for the array parameters of a module
    char array_pending[smith_lang_param_max]; // the last name before a `[`
    bool agg_pending;                         // struct, union or enum seen, `{` not yet
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
    char prev_tag_word; // 's', 'u' or 'e' for the struct, union or enum that prev_tag says
    bool typeish;
    char prev_char;
    uint32_t due[smith_lang_due_max]; // the tags (indexes into names) whose typedef goes before the next declaration
    uint32_t due_count;
    struct {                // tags a block defined, whose typedef follows the `;` that ends their declaration
        SmithLangText name; // as written: a block's tag is not the module's, no prefix
        char word;
        uint32_t depth; // the brace depth of the declaration
        uint32_t location;
    } locals[smith_lang_due_max];
    uint32_t local_count;
    uint32_t clash_at; // 1 + where the last tag clash was reported
    uint32_t due_late; // 1 + the tag whose typedef is due when the declaration being read ends (a payload enum's)
    // Lookahead and the batch.
    SmithToken queue[smith_lang_queue_max];
    size_t queue_head;
    size_t queue_count;
    size_t batch_in;
    uint8_t taken_close; // how the last tokens taken read: 1 after a `)`, 2 after `) ->` (a lambda's return type)
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
    char include_body[smith_lang_include_max]; // the `#include` lines of the header (the C imports), with the
                                               // conditionals around them
    size_t include_used;
    SmithLangText cimport_text[smith_lang_cimports_max]; // each `import <h>;`, in order: the name of the header
    bool cimport_angled[smith_lang_cimports_max];
    bool cimport_keep[smith_lang_cimports_max]; // whether the header has it (smith_lang_resolve_imports)
    size_t cimports_count;
    SmithLangCUse c_uses[smith_lang_cuses_max];
    size_t c_uses_count;
    bool imports_resolved;
    bool export_active;
    bool tee;            // the host is a raw run: private declarations are kept for the header's instance section
    bool private_active; // the tokens of the private declaration being read go to priv_text
    uint32_t priv_range; // 1 + the range of that declaration (0: none)
    size_t priv_used;
    char priv_text[smith_lang_priv_text_max];
    SmithLangPrivRange priv_ranges[smith_lang_priv_ranges_max];
    size_t priv_ranges_count;
    SmithLangPrivName priv_names[smith_lang_priv_names_max];
    size_t priv_names_count;
    SmithLangPrivNote priv_notes[smith_lang_priv_notes_max];
    size_t priv_notes_count;
    bool priv_resolved;                          // the ranges the generics need are chosen
    SmithLangText linked[smith_lang_linked_max]; // a compile: the private names that have a symbol (the header says)
    size_t linked_count;
    bool linked_read;
    SmithLangGenUse gen_uses[smith_lang_gen_uses_max];
    size_t gen_uses_count;
    SmithLangText hidden[smith_lang_hidden_max]; // as lowered: the imports' private names that their headers declare
    size_t hidden_count;
    bool emit_synthetic;   // everything emitted is a copy with no token of its own in the source (a prototype)
    bool aligned;          // the header body keeps the source's lines (an interface read in memory, runtime/lang_lex.c)
    size_t aligned_line;   // the line of the header body that is being written (from 1)
    size_t aligned_column; // the column of the next character on it (from 1)
    size_t header_used;
    bool dump_enabled;
    size_t dump_used;
    bool dump_bol;
    // The bodies of the module's functions, for importers that run them at compile time (runtime/lang_body.c).
    bool body_enabled;                                   // SMITH_LANG_BODY: the compile keeps each declaration's text
    bool body_closed;                                    // the instances are being written: no more sections
    size_t body_mark;                                    // where in the dump the next section begins
    size_t body_count;                                   // sections kept
    SmithLangText body_names[smith_lang_body_names_max]; // the exported functions with a body (`// smith-bodies:`)
    size_t body_names_count;
    uint32_t allowed[smith_lang_allowed_max];
    size_t allowed_count;
    size_t allowed_next;
    char tmp[smith_lang_name_max * 2];
    size_t tmp_length;
    // `target::` and `if constexpr` outside generics (runtime/lang_target.c).
    int32_t rfor[smith_lang_rfor_depth_max]; // range `for` over a slice: the brace count outside its block
    size_t rfor_count;
    int32_t rfor_braces;
    SmithLangIfFrame ifc[smith_lang_if_depth_max];
    size_t ifc_count;
    int32_t ifc_braces;
    bool ifc_opening;  // a file-scope `if constexpr`'s condition is being read: its facts and `#if` go into the header
    bool ifc_dropping; // a branch not taken is being skipped: its tokens are read but name nothing
    SmithLangConstant constants[smith_lang_constants_max];
    size_t constants_count;
    uint32_t target_used;         // the raw lowering: a bit for each fact the output uses
    uint32_t header_target_used;  // the same for an exported inline function's `if constexpr`: the header defines them
    uint32_t include_target_used; // the facts the `#if` lines around imports (a file-scope `if constexpr`) ask
    uint32_t include_prelude;     // those the include lines of the header define themselves
};

extern char smith_lang_chars[smith_lang_chars_max];

extern char smith_lang_scratch[smith_lang_scratch_max];

extern char smith_lang_header_body[smith_lang_header_max];

extern char smith_lang_header_full[smith_lang_header_max + 4096];

extern char smith_lang_dump[smith_lang_dump_max];

extern SmithLang smith_lang_instance;

// The counters that make the names of a file's temporaries unique (`smith_s3`, `smith_rfor_i2`). They count over one
// lowering, so a second lowering in the same process (smith_lang_reset, runtime/lang_lex.c) starts them again and
// writes the same names as the first.
constexpr unsigned smith_lang_counter_cut_start = 1000000;

constexpr unsigned smith_lang_counter_convert_start = 3000000;

typedef struct {
    unsigned rfor_pattern;
    unsigned slice_pattern;
    unsigned slice_subscript;
    unsigned slice_element;
    unsigned slice_cut;
    unsigned slice_convert;
} SmithLangCounter;

extern SmithLangCounter smith_lang_counter;

typedef struct {
    const char *name;
    const char *standard; // what the compiler sees: the compiler's own name for the type, so no header is needed
    const char *header;   // what a generated header says
} SmithLangSized;

extern const SmithLangSized smith_lang_sized[13];

bool smith_lang_tuples_on(const SmithLang *lang, const SmithHost *host);

bool smith_lang_type_on(const SmithLang *lang, const SmithHost *host);

[[gnu::always_inline]] bool smith_lang_equals(const char *text, size_t length, const char *literal);

[[gnu::always_inline]] bool smith_lang_punct(const SmithToken *token, const char *literal);

[[gnu::always_inline]] bool smith_lang_ident(const SmithToken *token, const char *literal);

void smith_lang_word_buckets(void);

SmithLangWord smith_lang_word(const char *text, size_t length);

bool smith_lang_is_word(const SmithToken *token);

const char *smith_lang_capital_name(const char *text, size_t length);

size_t smith_lang_capitals_lower(const char *text, size_t length, char *out, bool concepts);

const SmithLangSized *smith_lang_find_sized(const char *text, size_t length);

SmithLangText smith_lang_store(SmithLang *lang, const char *text, size_t length);

const char *smith_lang_str(SmithLangText text);

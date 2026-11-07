// Generated split of smith.c (#2685): what runtime/lang_generic.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"
#include "lang_out.h"

extern SmithLangGeneric smith_lang_generics[smith_lang_generics_max];

typedef struct {
    uint32_t start; // the tokens of one parameter: [start, end) in smith_lang_gt
    uint32_t end;
    int8_t type_index; // which `type T` or `constexpr auto f` it is (-1: any other parameter)
    uint8_t kind;      // a SmithLangSlot (Plain for a parameter that is no slot)
} SmithLangParam;

typedef struct {
    uint32_t generic;
    uint32_t arg_first[smith_lang_entries_max]; // the type arguments, lowered, in smith_lang_it: a slot each, and the
    uint32_t arg_count[smith_lang_entries_max]; // types of `auto... rest` where its slot is
    uint8_t entries;                            // how many (the last slot's list may be empty)
    SmithLangText key;
    SmithLangText cname;
    SmithLangText label;
    SmithLangText description;
    uint32_t call_location;
    uint32_t depth;
    uint32_t parent;        // 1 + the instance whose body asked for it (0: the file's own code)
    uint8_t result_state;   // a computed result type (issue #102): 0 not yet asked, 1 in `result`, 2 failed (reported)
    char result[160];       // its spelling
    uint64_t result_handle; // and its type's handle (a slice is written with its struct, runtime/lang_type.c)
    SmithLangText slices; // the slice structs of its types (issue #119): `slot\x1ftag\x1fdata type\x1fcount\x1e`, each
} SmithLangInstance;

extern SmithLangInstance smith_lang_instances[smith_lang_instances_max];

const char *smith_lang_gt_str(const SmithLangGenToken *token);

bool smith_lang_gt_is(const SmithLangGenToken *token, SmithTokenKind kind, const char *literal);

bool smith_lang_gt_punct(const SmithLangGenToken *token, const char *literal);

bool smith_lang_gt_open(const SmithLangGenToken *token);

bool smith_lang_gt_close(const SmithLangGenToken *token);

bool smith_lang_is_type_param(const SmithLang *lang, const char *name, size_t length);

void smith_lang_capture_mark(SmithLang *lang, uint32_t index);

void smith_lang_capture_fold(SmithLang *lang);

extern const char *const smith_lang_concepts[10];

bool smith_lang_concept_word(const SmithToken *token);

bool smith_lang_concept_holds(const SmithHost *host, unsigned concept_id, const char *spelling);

bool smith_lang_param_is_lambda(const SmithLangParam *param);

size_t smith_lang_generic_params(const SmithLangGeneric *generic, SmithLangParam *out, size_t capacity);

bool smith_lang_generic_candidate(const SmithLang *lang, const SmithHost *host, const SmithToken *first);

bool smith_lang_generic_ahead(SmithLang *lang, const SmithHost *host, const SmithToken *first);

void smith_lang_generic_note_capture(SmithLang *lang, const SmithToken *token);

void smith_lang_generic_note_binding(SmithLang *lang, uint16_t via);

void smith_lang_generic_flush_head(SmithLang *lang, const SmithHost *host, const SmithToken *terminator);

void smith_lang_generic_finish(SmithLang *lang);

size_t smith_lang_generic_lines(const SmithLang *lang, char *out, size_t capacity, size_t used);

size_t smith_lang_types_line(const SmithLang *lang, char *out, size_t capacity, size_t used);

extern char smith_lang_gen_dump[(size_t)1 << 20];

void smith_lang_gen_note_break(SmithLang *lang);

void smith_lang_gen_put_expanding(SmithLang *lang, const SmithHost *host, SmithTokenKind kind, const char *text,
                                         uint32_t location, uint32_t instance, bool expand);

void smith_lang_gen_put(SmithLang *lang, const SmithHost *host, SmithTokenKind kind, const char *text,
                               uint32_t location, uint32_t instance);

uint32_t smith_lang_gen_place(const SmithHost *host, const SmithLangGeneric *generic, SmithLangGenToken *token,
                                     uint32_t fallback);

void smith_lang_gen_put_token(SmithLang *lang, const SmithHost *host, const SmithLangPool *pool,
                                     const SmithLangGenToken *token, uint32_t location, uint32_t instance, bool real);

size_t smith_lang_it_spelling(uint32_t first, uint32_t count, char *out, size_t capacity);

bool smith_lang_has_pack(const SmithLangGeneric *generic);

size_t smith_lang_pack_base(const SmithLangGeneric *generic);

size_t smith_lang_pack_size(const SmithLangGeneric *generic, const SmithLangInstance *instance);

constexpr size_t smith_lang_replay_depth_max = 256;

typedef struct {
    uint32_t start; // [start, end) of smith_lang_gt
    uint32_t end;
} SmithLangSpan;

void smith_lang_gen_put_arg(SmithLang *lang, const SmithHost *host, const SmithLangInstance *instance,
                                   unsigned param, uint32_t instance_id);

void smith_lang_gen_slice_tag_text(SmithLang *lang, const SmithHost *host, const SmithLangInstance *instance,
                                          unsigned param, uint32_t location, char *tag, size_t capacity);

uint32_t smith_lang_generic_skip_attributes(const SmithLangGeneric *generic);

void smith_lang_gen_put_run(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                   const SmithLangInstance *instance, uint32_t from, uint32_t to, uint32_t location,
                                   uint32_t instance_id);

constexpr size_t smith_lang_pack_name_max = 64; // characters of the argument list's or a loop variable's name

constexpr size_t smith_lang_vars_max = 40;      // `for constexpr` loops open at once, and the numbers of an instance

// The variable of a `for constexpr`: a number while the loop is written out.
typedef struct {
    char name[smith_lang_pack_name_max];
    uint16_t length;
    int64_t value;
} SmithLangVar;

typedef enum {
    SmithLangFrame_Tokens,      // [pos, end) of smith_lang_gt
    SmithLangFrame_Loop,        // a `for constexpr`
    SmithLangFrame_TypedStart,  // the arguments of a call that holds a call are written to the pool, to be typed
    SmithLangFrame_TypedFinish, // ... and the call is written, now that they are typed
    SmithLangFrame_CountFinish, // the member `countof` takes is typed, and the count is written (lang_fields.c)
} SmithLangFrameAction;

typedef struct {
    uint32_t cond_start; // the condition, [start, end) of smith_lang_gt
    uint32_t cond_end;
    uint32_t step_start;
    uint32_t step_end;
    uint32_t body_start; // the statement written once for each turn
    uint32_t body_end;
    uint32_t iterations;
    bool stepping;       // the body has run: the step comes before the condition
    bool wrap;           // the body is one statement without braces: each turn is written in braces of its own
    bool range;          // `for constexpr (auto x : list)`: each turn's body names the element for `x`
    uint32_t alias;      // range: the token of the generic that names the element
    uint32_t list_start; // range: the list, [start, end) of smith_lang_gt
    uint32_t list_end;
    uint32_t pool_first; // range: where the pool was before the loop's condition and step were made, and
    uint32_t pool_mark;  // after (each turn's body is copied above it)
    uint32_t text_first;
    uint32_t text_mark;
} SmithLangLoop;

typedef struct {
    uint32_t pos; // [pos, end) of smith_lang_gt, or a punctuator to write
    uint32_t end;
    char literal;
    char words[640]; // words to write, separated by spaces (a slice's index in an instance, issue #119)
    uint32_t place;  // of a punctuator to write (0: the call that asked for the instance)
    bool line_start; // the words begin a line, as the generic's token they stand for does
    uint32_t begin;  // where the frame's tokens began (an empty `rest...` looks at the token before it)
    uint8_t action;  // SmithLangFrameAction
    bool started;    // TypedFinish: the typing flag to restore
    int32_t source;  // TypedStart: the finish
    SmithLangLoop loop;
    uint32_t pool_first; // TypedFinish: where in smith_lang_gt the captured arguments begin
    uint32_t pool_text;
    uint32_t group; // TypedFinish: the call's CallBegin; `end` is its CallEnd
    uint16_t generic_index;
} SmithLangFrame;

constexpr size_t smith_lang_junk_max = 64;

typedef struct {
    SmithLangFrame frames[smith_lang_replay_depth_max];
    size_t count;
    SmithLangVar vars[smith_lang_vars_max];
    size_t var_count;
    const SmithLangInstance *instance; // whose type arguments are written for T (none: the file's own code)
    uint32_t instance_id;
    const SmithLangGeneric *generic; // whose tokens these are
    uint32_t location;               // of the call that asked for the instance
    bool start_line;                 // and the call as written begins its line
    bool typing; // the tokens are written to be typed by the compiler: a parameter of the instance stands for a value
    // The call being finished holds a call: its arguments as the instance writes them, for the compiler to type.
    SmithLangSpan spans[smith_lang_call_params_max]; // the arguments as the generic's tokens have them
    SmithLangSpan typed[smith_lang_call_params_max]; // and as captured (same count)
    size_t typed_count;
    uint32_t typed_first; // the captured arguments of the call being finished: [first, last) of smith_lang_gt
    uint32_t typed_last;
    // Tokens in the pool that a capture holds but are not part of it: the written-out lists and patterns of a call or
    // a pattern in the arguments, read by the replay and never by the compiler (smith_lang_typed_compact).
    SmithLangSpan junk[smith_lang_junk_max];
    size_t junk_count;
    // `try` and `errdefer` (runtime/lang_enum.c): the braces open in the body, the flags of the open `errdefer`s and
    // the numbers of the names a `try` and a `return` write.
    uint32_t braces;
    uint32_t try_next;
    uint32_t flag_count;
    struct {
        uint32_t id;
        uint32_t depth;
    } flags[16];
} SmithLangReplay;

bool smith_lang_replay_push(SmithLangReplay *replay, uint32_t pos, uint32_t end, char literal);

// What an identifier of the body names, when it is the argument list `auto... rest`: its elements are the instance's
// parameters `rest_0`, ...
typedef struct {
    char name[smith_lang_pack_name_max];
    size_t count;
} SmithLangPackRef;

bool smith_lang_member_before(uint32_t at);

bool smith_lang_pack_resolve(const SmithLangReplay *replay, uint32_t at, SmithLangPackRef *ref);

bool smith_lang_pack_countof_at(const SmithLangReplay *replay, uint32_t at, uint32_t limit,
                                       SmithLangPackRef *ref);

bool smith_lang_var_value(const SmithLangReplay *replay, uint32_t at, int64_t *value);

uint32_t smith_lang_group_end(uint32_t begin, uint32_t limit);

bool smith_lang_stage_type(SmithLangSpan span, const SmithLangInstance *instance, uint32_t fallback,
                                  uint32_t *first, uint32_t *count);

extern uint32_t smith_lang_written_used;

void smith_lang_infer_written(SmithLangSpan span, char *out, size_t capacity);

bool smith_lang_infer_text(SmithLangSpan span, const SmithLangInstance *instance, uint32_t location, char *out,
                                  size_t capacity);

int smith_lang_param_type_of(const SmithLangGeneric *generic, const SmithLangInstance *instance,
                                    SmithLangSpan span);

int smith_lang_generic_of(SmithLang *lang, const SmithHost *host, const char *text, size_t length);

void smith_lang_concept_why(const SmithLang *lang, const SmithHost *host, uint32_t error_location,
                                   unsigned concept_id, const char *judged);

void smith_lang_generics_load(SmithLang *lang, const SmithHost *host, const char *module);

int smith_lang_generic_resolve(SmithLang *lang, const SmithHost *host, const char *module, size_t module_length,
                                      const char *member, size_t member_length);

void smith_lang_member_name(const char *module, size_t module_length, const char *member, size_t member_length,
                                   char *out, size_t capacity);

void smith_lang_callee_name(const SmithLang *lang, const char *module, size_t module_length, const char *member,
                                   size_t member_length, char *out, size_t capacity);

extern bool smith_lang_replay_after_open;

void smith_lang_replay_run(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay);

void smith_lang_replay_begin(SmithLangReplay *replay, const SmithLangGeneric *generic,
                                    const SmithLangInstance *instance, uint32_t id, uint32_t location);

void smith_lang_generic_instances(SmithLang *lang, const SmithHost *host);

void smith_lang_gen_mark(SmithLang *lang, SmithLangGenSpecial special, const char *text, size_t length,
                                uint32_t location);

void smith_lang_generic_poison(SmithLang *lang, const SmithHost *host, uint32_t location);

void smith_lang_generic_advance(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_generic_unclosed(SmithLang *lang, const SmithHost *host);

bool smith_lang_generic_call(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                    const SmithLangBinding *binding, const SmithToken *member);

bool smith_lang_generic_call_here(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                         const SmithLangName *known);

void smith_lang_generic_private_name(SmithLang *lang, const SmithHost *host, SmithLangName *known,
                                            const char *name, size_t length, uint32_t location);

void smith_lang_generics_aside(bool save, const SmithLang *lang);

void smith_lang_generics_reset(void);


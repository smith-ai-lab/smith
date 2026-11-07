// Generated split of smith.c (#2685): what runtime/make_expand.c shares with the other files.
#pragma once

#include "core.h"
#include "make.h"

constexpr size_t smith_make_depth_max = 96;

constexpr size_t smith_make_args_max = 16;

constexpr uint64_t smith_make_steps_max = (uint64_t)1 << 32;

typedef struct {
    const char *p;
    size_t n;
} SmithMakeSpan;

typedef enum {
    SmithMakeFn_Var,
    SmithMakeFn_Mark,
    SmithMakeFn_Subst,
    SmithMakeFn_Patsubst,
    SmithMakeFn_Strip,
    SmithMakeFn_Findstring,
    SmithMakeFn_Filter,
    SmithMakeFn_FilterOut,
    SmithMakeFn_Sort,
    SmithMakeFn_Word,
    SmithMakeFn_Wordlist,
    SmithMakeFn_Words,
    SmithMakeFn_Firstword,
    SmithMakeFn_Lastword,
    SmithMakeFn_Join,
    SmithMakeFn_Dir,
    SmithMakeFn_Notdir,
    SmithMakeFn_Suffix,
    SmithMakeFn_Basename,
    SmithMakeFn_Addsuffix,
    SmithMakeFn_Addprefix,
    SmithMakeFn_Wildcard,
    SmithMakeFn_Abspath,
    SmithMakeFn_Realpath,
    SmithMakeFn_Shell,
    SmithMakeFn_Eval,
    SmithMakeFn_Error,
    SmithMakeFn_Warning,
    SmithMakeFn_Info,
    SmithMakeFn_Origin,
    SmithMakeFn_Flavor,
    SmithMakeFn_Value,
    SmithMakeFn_If,
    SmithMakeFn_Or,
    SmithMakeFn_And,
    SmithMakeFn_Foreach,
    SmithMakeFn_Call,
} SmithMakeFn;

// A variable's state, saved while a foreach or a call lends it a value.
typedef struct {
    SmithMakeVar *var;
    SmithMakeVar state;
} SmithMakeSaved;

typedef struct {
    bool is_call;
    bool top;     // a whole variable used here is marked as a use (make.c)
    bool pending; // an argument was pushed and its end is not recorded yet
    SmithMakeFn fn;
    size_t stage;
    const char *src; // a text frame: what it scans
    size_t pos;
    size_t end;
    size_t start; // a call frame: where its result goes in the buffer
    SmithMakeSpan raw[smith_make_args_max];
    size_t raw_count;
    size_t done[smith_make_args_max]; // the buffer's length once argument i was expanded
    size_t cursor;
    size_t aux;
    const char *text; // foreach: the list; var: the substitution's patterns
    SmithMakeSaved saved[smith_make_args_max + 1];
    size_t saved_count;
} SmithMakeFrame;

typedef struct {
    SmithMakeFrame frames[smith_make_depth_max];
    size_t depth;
    SmithMakeBuffer buffer;
    SmithMakeBuffer scratch;
} SmithMakeMachine;

extern SmithMakeMachine smith_make_machine;

// The automatic variables of the rule being expanded, if any.
typedef struct {
    bool active;
    const char *target;
    const char *first;
    const char *prerequisites; // $^, each once
    const char *all;           // $+
    const char *newer;         // $?
    const char *order_only;    // $|
    const char *stem;
} SmithMakeAuto;

extern SmithMakeAuto smith_make_auto;

const char *smith_make_literal(const char *text, size_t length);

const char *smith_make_render(const char *text, size_t length);

// The C name of a Makefile variable in build.c (an option's variable or a named array): build_ and the name in lower
// case, as smith's naming rules ask (CFLAGS is build_cflags), and the same name each time it is asked for. Two
// variables whose names differ only in case, or a name another variable took, get a number.
constexpr size_t smith_make_idents_max = 8192;

typedef struct {
    const char *names[smith_make_idents_max]; // the Makefile's names
    const char *idents[smith_make_idents_max];
    size_t count;
} SmithMakeIdents;

extern SmithMakeIdents smith_make_idents;

const char *smith_make_ident(const char *name);

void smith_make_make_knob(SmithMakeVar *var, const char *fallback);

// The names of the empty variables already reported (smith_make_reset forgets them).
constexpr size_t smith_make_reported_max = 256;

extern const char *smith_make_reported[smith_make_reported_max];

extern size_t smith_make_reported_count;

const char *smith_make_expand_text(const char *text, bool top);

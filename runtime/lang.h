// The lowering of .smith files, and the interface between it and the program that gives it tokens.
//
// A .smith file is C with `module`, `import` and `export`, and sized type names (docs/smith-lang.md). The lowering is a
// state machine over the file's tokens: it is given one token at a time and answers with the C tokens that replace
// it. It knows nothing of clang. A host feeds it: the clang hook in shim/hook.cpp (tokens after macro expansion, with
// questions to the parser), or the plain lexer of runtime/lang_lex.c (a header written without compiling the module).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SmithTokenKind_Eof,
    SmithTokenKind_Identifier, // names and keywords alike: the lowering has its own keyword table
    SmithTokenKind_Number,
    SmithTokenKind_String,
    SmithTokenKind_Character,
    SmithTokenKind_Punctuator,
} SmithTokenKind;

constexpr uint32_t smith_token_no_handle = UINT32_MAX;

// One token. `text` is valid until the host is next asked for a token or the call to smith_lang_feed returns,
// whichever is first; the lowering copies what it keeps.
typedef struct {
    SmithTokenKind kind;
    bool line_start;   // the first token on its line
    bool space_before; // whitespace before it
    bool foreign;      // given by `next`: not spelled in the file being lowered (a header the stream entered)
    bool expand;       // emitted: macro-expand it when it re-enters the stream (the X of `c.X`)
    uint32_t location; // the host's source location, kept by every token derived from this one
    uint32_t handle;   // the host's own token, to re-emit unchanged (smith_token_no_handle: make one from the text)
    uint32_t length;
    const char *text;
    const char *header_text; // emitted: the spelling in a generated header, when it differs (uint64_t for u64)
    uint32_t header_length;
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

// What a sparse host offers besides the watched identifiers and the end of the file.
typedef enum {
    SmithWatch_Names,  // nothing else
    SmithWatch_Braces, // the braces
    SmithWatch_All,    // every token
} SmithWatch;

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
    // Watch every name the imported C headers declared (so that a bare use of one in a module is found).
    void (*watch_imports)(void *context);
    // Whether the file being lowered has declared anything yet (an import must come first).
    bool (*declared)(void *context);
    const char *main_file; // the path of the file being lowered
} SmithHost;

typedef struct SmithLang SmithLang;

SmithLang *smith_lang_new(void);
// Tells a sparse host the names to offer from the start: module, import, export and the sized types.
void smith_lang_watch_basics(const SmithHost *host);
// Gives the lowering one token. Returns true when the host must drop it and use what was emitted instead.
bool smith_lang_feed(SmithLang *lang, const SmithHost *host, const SmithToken *token);
// Whether a macro of an imported header may be expanded at this location (it was named c.X there).
bool smith_lang_c_use_allowed(const SmithLang *lang, uint32_t location);
bool smith_lang_is_module(const SmithLang *lang);
// In a module's function body, whether the host offers only braces and the names it watches (the host's fast paths).
bool smith_lang_body_sparse(const SmithLang *lang);
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

#ifdef __cplusplus
}
#endif

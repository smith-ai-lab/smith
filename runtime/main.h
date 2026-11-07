// Generated split of smith.c (#2685): what smith.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

bool smith_ends_with(const char *text, const char *suffix);

bool smith_takes_value(const char *arg);

bool smith_self_path(char *out);

void smith_run(const char *program, char *const *args);

void smith_resource_folder(char *folder);

void smith_programs_folder(const char *runtime, char *programs);

void smith_resource_ensure(const char *runtime, const char *programs, const char *self);

void smith_resource_directory(const char *runtime, char *out);

typedef struct {
    const char *name;
    const unsigned char *header;
    size_t header_size;
    const unsigned char *source;
    size_t source_size;
    bool lang;         // the source is a .smith module, not C
    const char *flags; // the flags the library declares, which `smith build` adds when it compiles it, or nullptr
} SmithLibrary;

#define SMITH_HEADER(library, is_lang) SMITH_HEADER_##is_lang(library)

#define SMITH_HEADER_SIZE(library, is_lang) SMITH_HEADER_SIZE_##is_lang(library)

#define SMITH_LIBRARY_FLAGS(library, is_lang, declared) \
    {#library, \
     SMITH_HEADER(library, is_lang), \
     SMITH_HEADER_SIZE(library, is_lang), \
     smith_##library##_source, \
     sizeof smith_##library##_source, \
     is_lang, \
     declared}

#define SMITH_LIBRARY(library, is_lang) SMITH_LIBRARY_FLAGS(library, is_lang, nullptr)

extern const SmithLibrary smith_libraries[32];

// `--time` and SMITH_TIME=1 (#2681): where the time goes, printed to stderr after the normal output as
// `time: <phase> <ms> ms`, nested phases indented. One monotonic clock read per phase, and none at all when it is off.
// The environment variable reaches every compile a build runs; the flag only sets it (smith_time_flag).
constexpr size_t smith_time_phases_max = 16;

constexpr size_t smith_time_steps_max = 10;

constexpr size_t smith_time_text_max = 160;

constexpr uint64_t smith_time_ns_per_ms = 1'000'000;

typedef struct {
    const char *name;
    int depth;
    uint64_t nanoseconds;
} SmithTimePhase;

typedef struct {
    char text[smith_time_text_max]; // "compile .c  runtime/slots.c"
    uint64_t nanoseconds;
} SmithTimeStep;

typedef struct {
    SmithTimePhase phases[smith_time_phases_max];
    size_t phase_count;
    SmithTimeStep slowest[smith_time_steps_max]; // longest first
    size_t slowest_count;
    size_t steps;
    uint64_t steps_nanoseconds;
    size_t cached;
    uint64_t cached_nanoseconds;
    uint64_t plan_nanoseconds; // the executor's, summed over the manifests it ran, until a build takes it
    uint64_t steps_run_nanoseconds;
    uint64_t front_nanoseconds; // `smith cc`: the new front end's share of the command
} SmithTimeState;

extern SmithTimeState smith_time_state;

bool smith_time_on(void);

uint64_t smith_time_now(void);

uint64_t smith_time_since(uint64_t start);

void smith_time_step(const char *kind, const char *what, uint64_t nanoseconds, bool cached);

constexpr size_t smith_build_args_max = 4096;

bool smith_lang_ask_compiler(const SmithLang *lang, const char **spellings);

void smith_write_embedded_headers(const char *self);

int smith_build_main(int count, char **args, const char *self, const char *verb);

// ---- smith's defaults for a project with no config file (issue #43, part 1)
//
// `smith fmt` and `smith lint` use smith's own .clang-format and .clang-tidy (templates/defaults, embedded) for a file
// the tool would find none for: clang-format's lookup (`.clang-format` or `_clang-format`, in the file's folder and
// each folder above) and clang-tidy's (`.clang-tidy`, the same walk). They are written to <data>/smith/defaults/ (named
// by their content) and passed as `--style=file:<path>` and `--config-file=<path>`. A project's file replaces smith's
// whole, and an explicit option of the tool wins. `--dump-config` is the tool's own, so it prints the effective one.
// `smith lsp` points clangd at `.smith/compile_commands.json` when the project has no root one.
constexpr int smith_defaults_args_max = 262144; // arguments of smith fmt, lint and lsp; more is an error

bool smith_only_options(int count, char **args);


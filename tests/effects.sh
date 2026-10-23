#!/bin/sh
# The clang-tidy check `smith-effects` (shim/effects.h; skill smith-io): a program touches the world only through the
# capabilities main passes down. A scratch project with the check alone, and a passing and a failing case for each rule:
# a raw call outside a capability's module; a pure call and a failure call; a raw call in a capability module's private
# helper; an exported function of that module that reaches a raw effect without a capability; a main-only constructor
# called outside main; a capability in static storage; an Objective-C message; `.smith` files with names as written
# (`c::fopen`); and the option CFiles. The editor's case is in tests/lsp.sh. A smith without clang inside (the stage0),
# or whose clang lacks the token hook, is skipped.
# Usage: sh tests/effects.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_DATA_HOME=$work/data
export XDG_CACHE_HOME=$work/cache
mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
export PATH=$work/bin:$PATH

failures=0
total=0

expect() { # name, expected, actual
    total=$((total + 1))
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        printf '%s\n' "$2" >"$work/expected"
        printf '%s\n' "$3" >"$work/actual"
        diff "$work/expected" "$work/actual" | head -30
    fi
}

if [ "$(smith cc --smith-which -c a.c | head -n 1 | cut -d ' ' -f 1,2)" != "clang in-process" ]; then
    echo "skipped: this smith has no clang inside (the stage0)"
    exit 0
fi
printf 'int main(void) { return 0; }\n' >"$work/probe.smith"
if smith cc --smith-lower "$work/probe.smith" 2>&1 | grep -q "token hook"; then
    echo "skipped: this smith's clang lacks the token hook"
    exit 0
fi

mkdir -p "$work/p/hw" "$work/p/lib" "$work/p/include"
cd "$work/p" || exit 1
printf 'Checks: "-*,smith-effects"\nWarningsAsErrors: "*"\n' >.clang-tidy
flags="-I$here/include -I. -std=gnu2y"

# The findings of a run, one line each, with the path of the project left off.
findings() {
    grep -E '^/.*:[0-9]+:[0-9]+: (error|warning):' | sed -e "s|^$work/p/||" -e 's/ \[smith-effects,-warnings-as-errors\]$//'
}
run() { # files...
    smith lint --quiet "$@" -- $flags 2>&1
}

# --- rule 1: a raw effect outside a capability's module; rule 2: failure is not one --------------------------------------
cat >raw.c <<'EOF'
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void raw(const char *path) {
    FILE *file = fopen(path, "r");
    (void)file;
    printf("hi\n");
    (void)getenv("HOME");
    char *memory = malloc(4);
    free(memory);
    exit(1);
}

void pure(const char *text, char *out, int *array, size_t count) {
    char buffer[16];
    snprintf(buffer, sizeof buffer, "%d", 1);
    memcpy(out, text, strlen(text));
    (void)sqrt(2.0);
    (void)strtol(text, nullptr, 10);
    (void)strcmp(text, out);
    qsort(array, count, sizeof array[0], nullptr);
    (void)__builtin_popcount(3u);
    int a = 0;
    (void)sscanf(text, "%d", &a);
}

void failure(const char *text) {
    assert(text != nullptr);
    if (text[0] == 'a') abort();
    if (text[0] == 'b') __builtin_trap();
    if (text[0] == 'c') __builtin_unreachable();
}
EOF
run raw.c >"$work/raw.out"
expect "a raw call outside a capability's module is reported, naming the capability to take" \
    "raw.c:8:18: error: \`fopen\` touches the world: take a \`SmithFiles *\` from main's SmithIo and read and write files through it (smith_files_read, smith_files_write)
raw.c:10:5: error: \`printf\` touches the world: take a \`SmithConsole *\` from main's SmithIo and print and read through it (smith_console_print, smith_console_read_line)
raw.c:11:11: error: \`getenv\` touches the world: take a \`SmithArgs *\` from main's SmithIo and read the environment through it (smith_args_env)
raw.c:12:20: error: \`malloc\` touches the world: take a \`SmithArena *\` from main's SmithIo and allocate from it (smith_arena_push, or arena::push in .smith)
raw.c:13:5: error: \`free\` touches the world: take a \`SmithArena *\` from main's SmithIo and allocate from it (smith_arena_push, or arena::push in .smith)
raw.c:14:5: error: \`exit\` touches the world: return the status from main instead" "$(findings <"$work/raw.out")"
expect "pure calls (string.h, math.h, snprintf, strto*, qsort, sscanf, builtins) are allowed, and so are the failure calls (assert, abort, __builtin_trap, __builtin_unreachable)" \
    "0" "$(findings <"$work/raw.out" | grep -c -e 'raw.c:\(1[7-9]\|[2-4][0-9]\)')"

# --- rule 3 and 4: a capability's module ----------------------------------------------------------------------------------
cat >lib/cap.h <<'EOF'
#pragma once
typedef struct [[clang::annotate("smith::capability")]] {
    int fd;
} Cap;
void cap_write(Cap *cap, const char *text);
void cap_leak(const char *text);
void cap_leak_deep(const char *text);
int cap_pure(int x);
Cap cap_open(void);
EOF
cat >lib/cap.c <<'EOF'
#include "cap.h"

#include <stdio.h>
#include <unistd.h>

static void helper(const char *text) {
    fputs(text, stdout);
}
static void middle(const char *text) {
    helper(text);
}
void cap_write(Cap *cap, const char *text) {
    (void)cap;
    middle(text);
}
void cap_leak(const char *text) {
    fputs(text, stdout);
}
void cap_leak_deep(const char *text) {
    middle(text);
}
int cap_pure(int x) {
    return x + 1;
}
Cap cap_open(void) {
    (void)write(1, "x", 1);
    return (Cap){1};
}
EOF
cat >lib/cap_user.c <<'EOF'
#include "cap.h"

#include <stdio.h>

void user(Cap *cap) {
    (void)cap;
    puts("not the module's file");
}
EOF
run lib/cap.c >"$work/cap.out"
expect "a capability module's private helpers may make raw calls; an exported function that reaches one, directly or through its static functions, must take or return a capability" \
    "lib/cap.c:16:6: error: \`cap_leak\` is exported by a capability's implementation and reaches \`fputs\`, but takes and returns no capability, so any caller could touch the world: take a capability, such as the \`SmithConsole *\` it needs, or make it static
lib/cap.c:19:6: error: \`cap_leak_deep\` is exported by a capability's implementation and reaches \`fputs\`, but takes and returns no capability, so any caller could touch the world: take a capability, such as the \`SmithConsole *\` it needs, or make it static" \
    "$(findings <"$work/cap.out")"
run lib/cap_user.c >"$work/cap_user.out"
expect "a file that merely uses the capability is not its implementation" \
    "lib/cap_user.c:7:5: error: \`puts\` touches the world: take a \`SmithConsole *\` from main's SmithIo and print and read through it (smith_console_print, smith_console_read_line)" \
    "$(findings <"$work/cap_user.out")"

# the pairing is io.h with io.c: lib/io.c beside an include/ folder is the implementation too
mkdir -p src include/proj
cat >include/proj/dev.h <<'EOF'
#pragma once
typedef struct [[clang::annotate("smith::capability")]] {
    int fd;
} Dev;
void dev_write(Dev *dev, const char *text);
EOF
cat >src/dev.c <<'EOF'
#include <proj/dev.h>

#include <stdio.h>

void dev_write(Dev *dev, const char *text) {
    (void)dev;
    fputs(text, stdout);
}
EOF
smith lint --quiet src/dev.c -- $flags -Iinclude >"$work/dev.out" 2>&1
expect "src/dev.c beside include/proj/dev.h is the implementation of Dev" "" "$(findings <"$work/dev.out")"

# --- rule 5: a main-only constructor ---------------------------------------------------------------------------------------
cat >made.c <<'EOF'
#include <smith/io.h>

int main(int argc, char **argv) {
    SmithIo io = smith_io_make(argc, argv);
    SmithIo fake = smith_io_make_test((SmithIoTest){});
    (void)fake;
    smith_io_close(&io);
    return 0;
}

SmithIo elsewhere(int argc, char **argv) {
    return smith_io_make(argc, argv);
}

SmithIo test_helper(void) {
    return smith_io_make_test((SmithIoTest){});
}
EOF
run made.c >"$work/made.out"
expect "smith_io_make is allowed in main, reported elsewhere, and a fake is allowed anywhere" \
    "made.c:12:12: error: \`smith_io_make\` makes a real capability and may be called only in main: take the capability as a parameter (a test makes a fake)" \
    "$(findings <"$work/made.out")"

# --- rule 6: no capability in static storage --------------------------------------------------------------------------------
cat >stored.c <<'EOF'
#include <smith/io.h>

typedef struct {
    SmithFiles *files;
} Bundle;

static SmithIo *global_io;
SmithClock *global_clock;
static Bundle bundle;
static int number;

void use(void) {
    static SmithConsole console;
    static int counter;
    (void)console;
    counter++;
}
EOF
run stored.c >"$work/stored.out"
expect "a capability, a pointer to one, a struct holding one, and a static local are reported; plain statics are not" \
    "stored.c:7:17: error: a capability cannot be kept in static storage (\`global_io\`); pass it as a parameter from main
stored.c:8:13: error: a capability cannot be kept in static storage (\`global_clock\`); pass it as a parameter from main
stored.c:9:15: error: a capability cannot be kept in static storage (\`bundle\`); pass it as a parameter from main
stored.c:13:25: error: a capability cannot be kept in static storage (\`console\`); pass it as a parameter from main" \
    "$(findings <"$work/stored.out")"

# --- Objective-C: a message to a system framework's class or object -----------------------------------------------------------
cat >objc.m <<'EOF'
#import <Foundation/Foundation.h>

@interface Mine : NSObject
- (int)value;
@end
@implementation Mine
- (int)value {
    return 1;
}
@end

int objc(void) {
    Mine *mine = [[Mine alloc] init];
    NSFileManager *manager = [NSFileManager defaultManager];
    return [mine value] + (int)[manager fileExistsAtPath:@"/tmp"];
}
EOF
smith lint --quiet objc.m -- -fobjc-arc >"$work/objc.out" 2>&1
expect "a message to a framework's class or object is reported; one to the project's own class is not" \
    "objc.m:14:30: error: \`[NSFileManager defaultManager]\` touches the world: wrap it in a capability's implementation (a module that defines a type marked [[clang::annotate(\"smith::capability\")]]) and take that capability as a parameter
objc.m:15:32: error: \`[NSFileManager fileExistsAtPath:]\` touches the world: wrap it in a capability's implementation (a module that defines a type marked [[clang::annotate(\"smith::capability\")]]) and take that capability as a parameter" \
    "$(findings <"$work/objc.out")"

# --- .smith: names as written ----------------------------------------------------------------------------------------------
cat >hw/dev.smith <<'EOF'
module hw::dev;

import <stdio.h>;
import <string.h>;

export typedef struct [[clang::annotate("smith::capability")]] {
    i32 fd;
} Dev;

static void helper(const char *text) {
    c::fputs(text, c::stdout);
}

export void write(Dev *dev, const char *text) {
    (void)dev;
    helper(text);
}

export void leak(const char *text) {
    helper(text);
}

export usize size(const char *text) {
    return c::strlen(text);
}

// An instance is compiled in the file that calls it, and is the code of this module: its raw call is the module's own.
export void emit(Dev *dev, auto value) {
    (void)dev;
    (void)value;
    c::fputs("x", c::stdout);
}
EOF
cat >hw_main.smith <<'EOF'
import <stdio.h>;
import <stdlib.h>;
import <smith/io.h>;
import hw::dev;

static dev::Dev *global_dev;

int main(int argc, char **argv) {
    c::SmithIo io = c::smith_io_make(argc, argv);
    c::printf("hi\n");
    dev::leak("x");
    c::abort();
    c::smith_io_close(&io);
    return 0;
}

void other(void) {
    c::exit(1);
    c::snprintf(nullptr, 0, "x");
    c::SmithIo again = c::smith_io_make(0, nullptr);
    (void)again;
    dev::emit(global_dev, 5);
}
EOF
run hw/dev.smith hw_main.smith >"$work/smith.out"
expect ".smith: the module's helpers may make raw calls, its leaking export is reported, and the callee is written c::name" \
    "hw/dev.smith:19:13: error: \`leak\` is exported by a capability's implementation and reaches \`c::fputs\`, but takes and returns no capability, so any caller could touch the world: take a capability, such as the \`SmithConsole *\` it needs, or make it static
hw_main.smith:6:18: error: a capability cannot be kept in static storage (\`global_dev\`); pass it as a parameter from main
hw_main.smith:10:5: error: \`c::printf\` touches the world: take a \`SmithConsole *\` from main's SmithIo and print and read through it (smith_console_print, smith_console_read_line)
hw_main.smith:18:5: error: \`c::exit\` touches the world: return the status from main instead
hw_main.smith:20:24: error: \`c::smith_io_make\` makes a real capability and may be called only in main: take the capability as a parameter (a test makes a fake)" \
    "$(findings <"$work/smith.out")"
expect ".smith: the lowering's names never appear" "0" "$(grep -c 'hw_dev_' "$work/smith.out")"
# The instance of `emit` in hw_main.smith is the module's code: a finding there (it has no place in a file, so the
# findings above do not list it) would say `fputs` touches the world.
expect ".smith: an instance counts as its generic's module" "0" "$(grep -c 'fputs. touches the world' "$work/smith.out")"

# --- the option CFiles ------------------------------------------------------------------------------------------------------
printf 'Checks: "-*,smith-effects"\nWarningsAsErrors: "*"\nCheckOptions:\n  smith-effects.CFiles: false\n' >.clang-tidy
run raw.c made.c hw_main.smith >"$work/off.out"
expect "CFiles false: a .c file is not checked, a .smith file is" \
    "hw_main.smith:6:18: error: a capability cannot be kept in static storage (\`global_dev\`); pass it as a parameter from main
hw_main.smith:10:5: error: \`c::printf\` touches the world: take a \`SmithConsole *\` from main's SmithIo and print and read through it (smith_console_print, smith_console_read_line)
hw_main.smith:18:5: error: \`c::exit\` touches the world: return the status from main instead
hw_main.smith:20:24: error: \`c::smith_io_make\` makes a real capability and may be called only in main: take the capability as a parameter (a test makes a fake)" \
    "$(findings <"$work/off.out")"
printf 'Checks: "-*,smith-effects"\nWarningsAsErrors: "*"\nCheckOptions:\n  smith-effects.CFiles: true\n' >.clang-tidy
run raw.c >"$work/on.out"
expect "CFiles true: the .c file is checked" "6" "$(findings <"$work/on.out" | grep -c 'raw.c')"

# --- smith's default .clang-tidy has the check, and --dump-config shows it --------------------------------------------------------
mkdir -p "$work/none"
cd "$work/none" || exit 1
cp "$work/p/raw.c" .
smith lint --dump-config >"$work/dump" 2>/dev/null
expect "smith lint --dump-config shows smith-effects among the checks" "1" "$(grep -c ', smith-effects' "$work/dump")"
smith lint --quiet raw.c -- $flags >"$work/default.out" 2>&1
expect "with no .clang-tidy the default checks report a raw call" "1" "$(grep -c 'fopen. touches the world' "$work/default.out")"

echo "effects: $((total - failures)) of $total passed"
[ $failures = 0 ]

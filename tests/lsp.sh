#!/bin/sh
# Tests of `smith lsp` on .smith files (issue #26): a scratch project of two modules and a C file, built by `smith build`,
# and a real `smith lsp` session over standard input and output (tests/lsp_client.c writes the JSON-RPC and prints what
# the server answered). Diagnostics, including the lowering's own, are checked at the lines and columns of the .smith text;
# so are hover, go-to-definition across modules, symbols and completion; and so is a C file of the same project, which
# must behave as it always has. A smith without clang inside (the stage0) or without the token hook is skipped.
# Usage: sh tests/lsp.sh path/to/smith
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
        echo "  expected: $2"
        echo "  actual:   $3"
    fi
}

expect_line() { # name, log, the whole line
    total=$((total + 1))
    if printf '%s\n' "$2" | grep -Fxq -- "$3"; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected the line: $3"
        printf '%s\n' "$2" | cut -c1-200 | sed 's/^/  actual: /'
    fi
}

if [ "$(smith cc --smith-which -c a.c | head -n 1 | cut -d ' ' -f 1,2)" != "clang in-process" ]; then
    echo "skipped: this smith has no clang inside (the stage0)"
    exit 0
fi
if ! smith lsp --version >/dev/null 2>&1; then
    echo "skipped: this smith has no clangd inside"
    exit 0
fi
printf 'int main(void) { return 0; }\n' >"$work/probe.smith"
if smith cc --smith-lower "$work/probe.smith" 2>&1 | grep -q "token hook"; then
    echo "skipped: this smith's clang lacks the token hook"
    exit 0
fi

mkdir -p "$work/p/geometry"
cd "$work/p" || exit 1
root=$(pwd -P)
cat >build.c <<'EOF'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu2y", "-Wall", "-Wextra", "-Wpedantic", "-Werror", nullptr};
    SmithObject main_object = {.compiler = "smith-cc", .source = "main.smith", .output = "build/main.o", .flags = flags};
    SmithObject box_object = {.compiler = "smith-cc", .source = "geometry/box.smith", .output = "build/box.o", .flags = flags};
    SmithObject util_object = {.compiler = "smith-cc", .source = "util.c", .output = "build/util.o", .flags = flags};
    smith_object(b, &main_object);
    smith_object(b, &box_object);
    smith_object(b, &util_object);
    SmithProgram program = {.linker = "smith-cc", .output = "build/demo", .objects = (const SmithObject *[]){&main_object, &box_object, &util_object, nullptr}};
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
EOF
cat >geometry/box.smith <<'EOF'
module geometry::box;

export typedef struct { f32 width; f32 height; } Box;

export f32 area(Box b) {
    return b.width * b.height;
}

f32 twice(f32 x) {
    return x * 2.0f;
}

export f32 perimeter(Box b) {
    return twice(b.width + b.height);
}
EOF
cat >typo.smith <<'EOF'
module typo;
import geometry::box;

f32 g(box::Box b) {
    return box::aera(b);
}
EOF
cat >qualified.smith <<'EOF'
module qualified;
import geometry::box;

f32 g(box::Box b) {
    return box::;
}
EOF
cat >main.smith <<'EOF'
module main;
import geometry::box;
import <stdio.h>;

int main(void) {
    box::Box b = {2.0f, 3.0f};
    f32 a = box::area(b);
    c::printf("area %f\n", (double)a);
    return 0;
}
EOF
cat >bad.smith <<'EOF'
module bad;
import geometry::box;
import <stdio.h>;

int main(void) {
    box::Box b = {2.0f, 3.0f};
    printf("area %f\n", (double)box::area(b));
    box::missing(b);
    return b.nothing;
}
EOF
cat >complete.smith <<'EOF'
module complete;
import geometry::box;

int f(void) {
    geometry_box_ar
}
EOF
cat >util.c <<'EOF'
#include <stdio.h>

int util_twice(int value) {
    return value * 2;
}

void util_note(void) {
    fprintf(stderr, "note\n");
}
EOF
cat >cbad.c <<'EOF'
int util_bad(void) {
    int x = "text";
    return x;
}
EOF

# The project builds with smith build, and runs.
out=$(smith build 2>&1)
status=$?
expect "the project builds" 0 "$status"
[ "$status" -eq 0 ] || printf '%s\n' "$out" | tail -n 20
expect "the program runs" "area 6.000000" "$(build/demo 2>&1)"
expect "compile_commands.json tells clangd the language of a .smith file" 2 \
    "$(grep -c '"-x"' compile_commands.json)"

if ! smith cc -std=gnu23 "$here/tests/lsp_client.c" -o "$work/lsp_client"; then
    echo "FAIL  tests/lsp_client.c does not build"
    exit 1
fi
client() { "$work/lsp_client" "$(command -v smith)" "$root" "$@" 2>&1; }

# Diagnostics: the three .smith files open together (their lowerings take turns), with nothing wrong in two of them and
# the lowering's own error and clang's in the third, at the lines and columns of the text as written, and naming things
# as they are written: `box::missing`, never the compiler's `geometry_box_missing`.
log=$(client open main.smith geometry/box.smith bad.smith)
expect_line "open: the clean module has no diagnostics" "$log" "diagnostics main.smith"
expect "open: and nothing else is reported for it" 0 "$(printf '%s\n' "$log" | grep -c '^diag main.smith')"
expect "open: the other module has none" 0 "$(printf '%s\n' "$log" | grep -c '^diag geometry/box.smith')"
expect_line "open: the lowering's error is at the name" "$log" 'diag bad.smith:7:5: `printf` is a C name; write `c::printf`'
expect_line "open: an unknown member of a module is clang's, at the member, named as written" "$log" \
    "diag bad.smith:8:10: Use of undeclared identifier 'box::missing'"
expect_line "open: a field that is not there, in a type named as written" "$log" \
    "diag bad.smith:9:14: No member named 'nothing' in 'box::Box'"
expect "open: and nothing more in it" 3 "$(printf '%s\n' "$log" | grep -c '^diag bad.smith')"

# A name close to a real one: "did you mean" says it as written, and the note is at the declaration in the module's
# .smith, not in the header that `smith build` wrote for it (.smith/include/geometry/box.h).
log=$(client open typo.smith geometry/box.smith)
expect_line "typo: the suggestion and its note are as written, the note is in the module" "$log" \
    "diag typo.smith:5:17: Use of undeclared identifier 'box::aera'; did you mean 'box::area'? (fix available) |  | geometry/box.smith:5:12: | note: 'box::area' declared here"

# An edit in the editor moves the diagnostic with the text.
log=$(client open main.smith -- replace main.smith 'box::area(b)' 'box::aria(b)')
expect_line "replace: the new error is at the changed name, as written" "$log" \
    "diag main.smith:7:18: Use of undeclared identifier 'box::aria'; did you mean 'box::area'? (fix available) |  | geometry/box.smith:5:12: | note: 'box::area' declared here"

# Hover, go-to-definition across modules (the answer is the .smith file, not the generated header), symbols: the names
# as the file writes them, `box::area` where it is imported and `area` in the module itself.
log=$(client open main.smith geometry/box.smith -- hover main.smith 7 18 -- hover main.smith 6 10 -- \
    definition main.smith 7 18 geometry/box.smith:5:12 -- definition main.smith 6 10 geometry/box.smith:3:50 -- \
    symbols main.smith -- hover geometry/box.smith 5 12 -- hover geometry/box.smith 3 50 -- symbols geometry/box.smith)
expect_line "hover: a call, the function as written" "$log" \
    'hover main.smith:7:18: function box::area |  | provided by "geometry/box.h" |  | → float |  | Parameters: |  | - box::Box b |  | float box::area(box::Box b)'
expect_line "hover: a type, as written" "$log" \
    'hover main.smith:6:10: type-alias box::Box |  | provided by "geometry/box.h" |  | Type: struct box::Box |  | typedef struct box::Box box::Box'
expect_line "definition: the function is in the other module's .smith, at its name" "$log" \
    "definition main.smith:7:18 -> geometry/box.smith:5:12"
expect_line "definition: so is the type" "$log" "definition main.smith:6:10 -> geometry/box.smith:3:50"
expect_line "symbols: main" "$log" "symbol main.smith: main"
expect_line "hover at the definition: the function's name, in the module itself" "$log" \
    "hover geometry/box.smith:5:12: function area |  | → f32 (aka float) |  | Parameters: |  | - Box b |  | f32 area(Box b)"
expect_line "hover at the definition: and the type's" "$log" \
    "hover geometry/box.smith:3:50: type-alias Box |  | Type: struct Box |  | typedef struct Box Box"
expect "symbols: a module's names, as written, each once" \
    "symbol geometry/box.smith: (anonymous struct) symbol geometry/box.smith: width symbol geometry/box.smith: height symbol geometry/box.smith: Box symbol geometry/box.smith: area symbol geometry/box.smith: twice symbol geometry/box.smith: perimeter" \
    "$(printf '%s\n' "$log" | grep '^symbol geometry/box.smith' | tr '\n' ' ' | sed 's/ $//')"

# A `c::` name is a name of the file like another: hover and go-to-definition at it find the one declaration (not every
# declaration of the name, which is what the index answers by name: the SDK's and libc++'s `using`), and the hover of a
# module's name has no label of its symbol. In a .c file of the same project the same requests are as they were.
log=$(client open main.smith util.c -- hover main.smith 8 8 -- definitions main.smith 8 8 -- hover main.smith 7 18 -- \
    hover util.c 8 5 -- definitions util.c 8 5)
case $log in *"hover main.smith:8:8: function printf |  | provided by <stdio.h>"*) chover=yes ;; *) chover=no ;; esac
expect "c::printf: hover" yes "$chover"
expect "c::printf: exactly one definition" "definitions main.smith:8:8: 1" "$(printf '%s\n' "$log" | grep '^definitions main.smith:8:8')"
expect "box::area: a hover without the label of the symbol" 0 "$(printf '%s\n' "$log" | grep '^hover main.smith:7:18' | grep -c 'asm(')"
case $log in *"hover util.c:8:5: function fprintf |  | provided by <stdio.h>"*) chover=yes ;; *) chover=no ;; esac
expect "c: fprintf in a .c file, hover" yes "$chover"
expect "c: fprintf in a .c file, exactly one definition" "definitions util.c:8:5: 1" "$(printf '%s\n' "$log" | grep '^definitions util.c:8:5')"

# The other requests at a definition: references, highlights and rename find the name once, at its place (the copy of the
# head that carries the symbol is no second declaration), and never go to the generated header.
log=$(client open main.smith geometry/box.smith -- references geometry/box.smith 5 12 -- highlight geometry/box.smith 5 12 -- \
    rename geometry/box.smith 9 5 thrice -- rename geometry/box.smith 3 50 Square)
expect "references: the definition and the call, nothing in .smith/include" \
    "reference geometry/box.smith:5:12 reference main.smith:7:18" \
    "$(printf '%s\n' "$log" | sed -n '/^references geometry\/box.smith:5:12/,/^highlights/p' | grep '^reference ' | tr '\n' ' ' | sed 's/ $//')"
expect "highlight: the name once" "highlight 5:12" "$(printf '%s\n' "$log" | grep '^highlight ' | tr '\n' ' ' | sed 's/ $//')"
expect_line "rename: a private function, at its definition" "$log" \
    "rename geometry/box.smith:9:5: geometry/box.smith:9:5 -> thrice"
expect_line "rename: and at its use" "$log" "rename geometry/box.smith:9:5: geometry/box.smith:14:12 -> thrice"
expect "rename: an edit is never in a generated header" 0 "$(printf '%s\n' "$log" | grep -c '^rename .*\.smith/include')"

# Completion of a name being typed. Anywhere it is offered as it is written, and inserted so (`box::area`, which the
# editor replaces the typed `geometry_box_ar` with).
log=$(client open complete.smith geometry/box.smith -- complete complete.smith 5 20)
expect_line "complete: a module's function by its prefix, as written, with its edit" "$log" \
    "complete complete.smith:5:20:  box::area(box::Box b) -> 5:5-5:20 box::area"

# After `box::` the module's exports are offered, as their members, and the text the editor has typed stays: the edit
# replaces only what comes after the `::`.
log=$(client open qualified.smith geometry/box.smith -- complete qualified.smith 5 17 -- \
    replace qualified.smith 'box::;' 'box::pe;' -- complete qualified.smith 5 19)
expect_line "complete after box::: a function, inserted as foo::f would be written" "$log" \
    "complete qualified.smith:5:17:  area(Box b) -> 5:17-5:17 area"
expect_line "complete after box::: another" "$log" "complete qualified.smith:5:17:  perimeter(Box b) -> 5:17-5:17 perimeter"
expect "complete after box::: and nothing else (no private function, no type, no C name)" 2 \
    "$(printf '%s\n' "$log" | grep -c '^complete qualified.smith:5:17: ')"
expect "complete after box::pe: what was typed narrows it, and the edit covers it" \
    "complete qualified.smith:5:19:  perimeter(Box b) -> 5:17-5:19 perimeter" \
    "$(printf '%s\n' "$log" | grep '^complete qualified.smith:5:19: ')"

# Plain C in a .c file of the same project is C: its error at its place, hover as ever.
log=$(client open util.c cbad.c -- hover util.c 3 5)
expect_line "c: the error is clang's, at its place" "$log" \
    "diag cbad.c:2:9: Incompatible pointer to integer conversion initializing 'int' with an expression of type 'char[5]'"
case $log in *"hover util.c:3:5: function util_twice"*) chover=yes ;; *) chover=no ;; esac
expect "c: hover" yes "$chover"

# No project at all: a folder with no build.c and no compile_commands.json, where clangd's fallback command amounts to
# `clang main.smith`. A .smith file gets what `smith cc` gives it (`-x c`, `-std=gnu2y`: smith_lang_command), so the editor
# reads it as the CLI does.
cd "$work" || exit 1
mkdir -p solo modules handwritten borrowed
cat >solo/main.smith <<'EOF'
#include <stdio.h>

int main() {
  printf("hello world\n");
  return 0;
}
EOF
client_in() { # folder, client arguments
    dir=$1
    shift
    (cd "$dir" && "$work/lsp_client" "$(command -v smith)" "$dir" "$@" 2>&1)
}
solo=$(cd solo && pwd -P)
log=$(client_in "$solo" open main.smith)
expect_line "solo: a single file with no database is read" "$log" "diagnostics main.smith"
expect "solo: and has no diagnostics" 0 "$(printf '%s\n' "$log" | grep -c '^diag ')"
log=$(client_in "$solo" open main.smith -- replace main.smith 'return 0' 'return zzz' -- hover main.smith 4 4 -- \
    complete main.smith 4 7)
expect_line "solo: an undefined name is reported at its place" "$log" "diag main.smith:5:10: Use of undeclared identifier 'zzz'"
expect "solo: and only that" 1 "$(printf '%s\n' "$log" | grep -c '^diag ')"
case $log in *"hover main.smith:4:4: function printf"*) hover=yes ;; *) hover=no ;; esac
expect "solo: hover" yes "$hover"
case $log in *"complete main.smith:4:7: "*printf*) completes=yes ;; *) completes=no ;; esac
expect "solo: completion" yes "$completes"

# Two modules with no build.c: `import foo` is found by its source, in the place the module name implies.
cat >modules/main.smith <<'EOF'
module main;
import foo;
import <stdio.h>;

int main(void) {
    i32 x = foo::f(10);
    c::printf("%d\n", x);
    return 0;
}
EOF
cat >modules/foo.smith <<'EOF'
module foo;

export i32 f(i32 x) {
    return x * 2;
}

export typedef struct { i32 a; i32 b; } Pair;

export i32 sum(Pair p) {
    return p.a + p.b;
}
EOF
cat >modules/qualified.smith <<'EOF'
module qualified;
import foo;

i32 g(void) {
    return foo::;
}
EOF
modules=$(cd modules && pwd -P)
expect "modules: the same two files build and run with smith cc" 20 \
    "$(cd "$modules" && smith cc main.smith foo.smith -o demo 2>&1 && ./demo)"
log=$(client_in "$modules" open main.smith foo.smith)
expect "modules: both files open" 2 "$(printf '%s\n' "$log" | grep -c '^diagnostics ')"
expect "modules: with no diagnostics" 0 "$(printf '%s\n' "$log" | grep -c '^diag ')"
log=$(client_in "$modules" open main.smith foo.smith -- replace main.smith 'foo::f' 'foo::zzzzzz')
expect_line "modules: a typo in a call is one error, at the name, as written" "$log" \
    "diag main.smith:6:18: Use of undeclared identifier 'foo::zzzzzz'"
expect "modules: and the only one" 1 "$(printf '%s\n' "$log" | grep -c '^diag ')"
# A name close to a real one: the suggestion is as written, and the note is at the declaration in foo.smith (the module
# is read from its source, so there is no header; its interface keeps the module's lines and columns).
log=$(client_in "$modules" open main.smith foo.smith -- replace main.smith 'foo::f' 'foo::g')
expect_line "modules: a name close to a real one, the note in the module's source" "$log" \
    "diag main.smith:6:18: Use of undeclared identifier 'foo::g'; did you mean 'foo::f'? (fix available) |  | $modules/foo.smith:3:12: | note: 'foo::f' declared here"
log=$(client_in "$modules" open main.smith foo.smith -- hover main.smith 6 18 -- \
    definition main.smith 6 18 foo.smith:3:12 -- hover foo.smith 3 12 -- hover foo.smith 7 41 -- symbols foo.smith -- \
    references foo.smith 3 12 -- highlight foo.smith 3 12)
expect_line "modules: hover on a call into the other module, as written" "$log" \
    'hover main.smith:6:18: function foo::f |  | provided by "foo.smith" |  | → int32_t (aka int) |  | Parameters: |  | - int32_t x (aka int) |  | int32_t foo::f(int32_t x)'
expect_line "modules: go-to-definition lands in the other module's source" "$log" \
    "definition main.smith:6:18 -> foo.smith:3:12"
expect_line "modules: hover at the function's definition" "$log" \
    "hover foo.smith:3:12: function f |  | → i32 (aka int) |  | Parameters: |  | - i32 x (aka int) |  | i32 f(i32 x)"
expect_line "modules: hover at the type's definition" "$log" \
    "hover foo.smith:7:41: type-alias Pair |  | Type: struct Pair |  | typedef struct Pair Pair"
expect "modules: symbols, as written, each once" \
    "symbol foo.smith: f symbol foo.smith: (anonymous struct) symbol foo.smith: a symbol foo.smith: b symbol foo.smith: Pair symbol foo.smith: sum" \
    "$(printf '%s\n' "$log" | grep '^symbol ' | tr '\n' ' ' | sed 's/ $//')"
expect "modules: references of the function: its definition and its call" \
    "reference foo.smith:3:12 reference main.smith:6:18" \
    "$(printf '%s\n' "$log" | grep '^reference ' | tr '\n' ' ' | sed 's/ $//')"
expect "modules: highlights of it: the name once" "highlight 3:12" "$(printf '%s\n' "$log" | grep '^highlight ' | tr '\n' ' ' | sed 's/ $//')"
log=$(client_in "$modules" open qualified.smith foo.smith -- complete qualified.smith 5 17)
expect_line "modules: foo:: offers the module's function, inserted as written" "$log" \
    "complete qualified.smith:5:17:  f(int32_t x) -> 5:17-5:17 f"
expect_line "modules: and another" "$log" "complete qualified.smith:5:17:  sum(Pair p) -> 5:17-5:17 sum"
expect "modules: and nothing else" 2 "$(printf '%s\n' "$log" | grep -c '^complete ')"

# `c::` names of every kind, in a folder with no database: a function, a function of a local header, a type, a macro for a
# variable, a function-like macro and a constant macro. Each hover finds the declaration it names, and go-to-definition has
# exactly one answer.
mkdir -p cinterop
cat >cinterop/local.h <<'EOF'
static inline int local_value(void) { return 99; }
EOF
cat >cinterop/main.smith <<'EOF'
import <math.h>;
import <sys/wait.h>;
import <stdio.h>;
import "local.h";

int main(void) {
    f32 root = c::sqrtf(81.0f);
    int status = 256 * 3;
    c::FILE *f = c::stdin;
    c::printf("%.1f %d %d eof=%d %d\n", (double)root, c::WIFEXITED(status), c::WEXITSTATUS(status), c::EOF, c::local_value());
    return f == nullptr;
}
EOF
cinterop=$(cd cinterop && pwd -P)
expect "cinterop: it builds and runs" "9.0 1 3 eof=-1 99" "$(cd "$cinterop" && smith cc main.smith -o demo 2>&1 && ./demo)"
col() { awk -v n="$1" -v w="$2" 'NR == n { print index($0, w) }' "$cinterop/main.smith"; }
sqrtf_col=$(col 7 sqrtf)
file_col=$(col 9 FILE)
stdin_col=$(col 9 stdin)
printf_col=$(col 10 printf)
wifexited_col=$(col 10 WIFEXITED)
eof_col=$(col 10 EOF)
local_col=$(col 10 local_value)
log=$(client_in "$cinterop" open main.smith -- hover main.smith 7 "$sqrtf_col" -- hover main.smith 9 "$file_col" -- \
    hover main.smith 9 "$stdin_col" -- hover main.smith 10 "$printf_col" -- hover main.smith 10 "$wifexited_col" -- \
    hover main.smith 10 "$eof_col" -- hover main.smith 10 "$local_col" -- definitions main.smith 7 "$sqrtf_col" -- \
    definitions main.smith 9 "$file_col" -- definitions main.smith 9 "$stdin_col" -- \
    definitions main.smith 10 "$printf_col" -- definitions main.smith 10 "$wifexited_col" -- \
    definitions main.smith 10 "$eof_col" -- definitions main.smith 10 "$local_col")
expect "cinterop: no diagnostics" 0 "$(printf '%s\n' "$log" | grep -c '^diag ')"
hovered() { # line, column, the start of the text
    printf '%s\n' "$log" | grep -c "^hover main.smith:$1:$2: $3"
}
expect "cinterop: hover on a function, c::sqrtf" 1 "$(hovered 7 "$sqrtf_col" 'function sqrtf |  | provided by <math.h>')"
expect "cinterop: hover on a type, c::FILE" 1 "$(hovered 9 "$file_col" 'type-alias FILE |  | provided by <stdio.h>')"
expect "cinterop: hover on a macro for a variable, c::stdin" 1 "$(hovered 9 "$stdin_col" 'macro stdin |  | provided by <stdio.h>')"
expect "cinterop: hover on a variadic function, c::printf" 1 "$(hovered 10 "$printf_col" 'function printf |  | provided by <stdio.h>')"
expect "cinterop: hover on a function-like macro, c::WIFEXITED" 1 \
    "$(hovered 10 "$wifexited_col" 'macro WIFEXITED |  | provided by <sys/wait.h>')"
expect "cinterop: hover on a constant macro, c::EOF" 1 "$(hovered 10 "$eof_col" 'macro EOF |  | provided by <stdio.h>')"
expect "cinterop: hover on a function of a local header, c::local_value" 1 \
    "$(hovered 10 "$local_col" 'function local_value |  | provided by "local.h"')"
for pair in "7 $sqrtf_col" "9 $file_col" "9 $stdin_col" "10 $printf_col" "10 $wifexited_col" "10 $eof_col" "10 $local_col"; do
    set -- $pair
    expect "cinterop: exactly one definition at $1:$2" "definitions main.smith:$1:$2: 1" \
        "$(printf '%s\n' "$log" | grep "^definitions main.smith:$1:$2:")"
done
expect_line "cinterop: c::local_value is defined in its header, at its name" "$log" "  place local.h:1:19"

# A compile database that does not say what a .smith file is: written by hand without `-x c`, or naming only a .c file
# (the .smith files then get a command borrowed from it).
cp modules/main.smith modules/foo.smith handwritten/
cp modules/main.smith modules/foo.smith borrowed/
printf 'int util(int x) { return x; }\n' >borrowed/util.c
cat >handwritten/compile_commands.json <<EOF
[{"directory": "$work/handwritten", "command": "smith-cc -c main.smith -o main.o -std=gnu23", "file": "main.smith"},
 {"directory": "$work/handwritten", "command": "smith-cc -c foo.smith -o foo.o", "file": "foo.smith"}]
EOF
cat >borrowed/compile_commands.json <<EOF
[{"directory": "$work/borrowed", "command": "smith-cc -c util.c -o util.o -Wall", "file": "util.c"}]
EOF
for dir in handwritten borrowed; do
    log=$(client_in "$(cd "$work/$dir" && pwd -P)" open main.smith foo.smith -- definition main.smith 6 18 foo.smith:3:12 -- \
        replace main.smith 'foo::f' 'foo::zzzzzz')
    expect_line "$dir: the call is found in the other module" "$log" "definition main.smith:6:18 -> foo.smith:3:12"
    expect_line "$dir: a typo is one error, at the name" "$log" \
        "diag main.smith:6:18: Use of undeclared identifier 'foo::zzzzzz'"
    expect "$dir: and the only one" 1 "$(printf '%s\n' "$log" | grep -c '^diag ')"
done

echo
echo "$total language server checks, $failures failed"
[ "$failures" -eq 0 ]

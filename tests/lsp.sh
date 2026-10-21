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
# the lowering's own error and clang's in the third, at the lines and columns of the text as written.
log=$(client open main.smith geometry/box.smith bad.smith)
expect_line "open: the clean module has no diagnostics" "$log" "diagnostics main.smith"
expect "open: and nothing else is reported for it" 0 "$(printf '%s\n' "$log" | grep -c '^diag main.smith')"
expect "open: the other module has none" 0 "$(printf '%s\n' "$log" | grep -c '^diag geometry/box.smith')"
expect_line "open: the lowering's error is at the name" "$log" 'diag bad.smith:7:5: `printf` is a C name; write `c::printf`'
expect_line "open: an unknown member of a module is clang's, at the member" "$log" \
    "diag bad.smith:8:10: Use of undeclared identifier 'geometry_box_missing'"
expect_line "open: a field that is not there" "$log" "diag bad.smith:9:14: No member named 'nothing' in 'geometry_box_Box'"
expect "open: and nothing more in it" 3 "$(printf '%s\n' "$log" | grep -c '^diag bad.smith')"

# An edit in the editor moves the diagnostic with the text.
log=$(client open main.smith -- replace main.smith 'box::area(b)' 'box::aria(b)')
total=$((total + 1))
if printf '%s\n' "$log" | grep -Fq "diag main.smith:7:18: Use of undeclared identifier 'geometry_box_aria'"; then
    echo "ok    replace: the new error is at the changed name"
else
    failures=$((failures + 1))
    echo "FAIL  replace: the new error is at the changed name"
    printf '%s\n' "$log" | cut -c1-200 | sed 's/^/  actual: /'
fi

# Hover, go-to-definition across modules (the answer is the .smith file, not the generated header), symbols.
log=$(client open main.smith -- hover main.smith 7 18 -- definition main.smith 7 18 geometry/box.smith:5:12 -- \
    definition main.smith 6 10 geometry/box.smith:3:50 -- symbols main.smith)
case $log in *"hover main.smith:7:18: function geometry_box_area"*"float"*) hover=yes ;; *) hover=no ;; esac
expect "hover: the function's signature" yes "$hover"
expect_line "definition: the function is in the other module's .smith, at its name" "$log" \
    "definition main.smith:7:18 -> geometry/box.smith:5:12"
expect_line "definition: so is the type" "$log" "definition main.smith:6:10 -> geometry/box.smith:3:50"
expect_line "symbols: main" "$log" "symbol main.smith: main"
log=$(client open geometry/box.smith -- symbols geometry/box.smith)
expect_line "symbols: a module's function, by its C name" "$log" "symbol geometry/box.smith: geometry_box_area"

# Completion of a name being typed: the C names the lowering gives.
log=$(client open complete.smith -- complete complete.smith 5 20)
case $log in *"complete complete.smith:5:20: "*"geometry_box_area"*) completes=yes ;; *) completes=no ;; esac
expect "complete: a module's function by its prefix" yes "$completes"

# Plain C in a .c file of the same project is C: its error at its place, hover as ever.
log=$(client open util.c cbad.c -- hover util.c 3 5)
expect_line "c: the error is clang's, at its place" "$log" \
    "diag cbad.c:2:9: Incompatible pointer to integer conversion initializing 'int' with an expression of type 'char[5]'"
case $log in *"hover util.c:3:5: function util_twice"*) chover=yes ;; *) chover=no ;; esac
expect "c: hover" yes "$chover"

echo
echo "$total language server checks, $failures failed"
[ "$failures" -eq 0 ]

#!/bin/sh
# Tests of `smith lsp` on .smith files (issue #26): a scratch project of two modules and a C file, built by `smith build`,
# and a real `smith lsp` session over standard input and output (tests/lsp_client.c writes the JSON-RPC and prints what
# the server answered). Diagnostics, including the lowering's own, are checked at the lines and columns of the .smith text;
# so are hover, go-to-definition across modules, symbols and completion; and so is a C file of the same project, which
# must behave as it always has. A smith without clang inside (the stage0) or without the token hook is skipped.
# Usage: sh tests/lsp.sh path/to/smith [project|folders]
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(pwd)
work=$(mktemp -d)
editor_work=$(mktemp -d)
trap 'rm -rf "$work" "$editor_work"' EXIT INT TERM
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
# These projects test the lowering's and the compiler's diagnostics, so smith's default checks (smith-effects reports the
# printf of its samples) are off here; the editor's checks are tested in the folders below, which have no .clang-tidy.
printf 'Checks: "-*"\n' >"$work/.clang-tidy"
part=${2:-all} # project, folders or all: the two halves run as tasks of their own (issue #116)
if ! smith cc -std=gnu23 "$here/tests/lsp_client.c" -o "$work/lsp_client"; then
    echo "FAIL  tests/lsp_client.c does not build"
    exit 1
fi
if [ "$part" != folders ]; then
cat >build.c <<'EOF'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu2y", "-Wall", "-Wextra", "-Wpedantic", "-Werror", nullptr};
    SmithObject main_object = {.compiler = "smith-cc", .source = "main.smith", .output = "build/main.o", .flags = flags};
    SmithObject box_object = {.compiler = "smith-cc", .source = "geometry/box.smith", .output = "build/box.o", .flags = flags};
    SmithObject util_object = {.compiler = "smith-cc", .source = "util.c", .output = "build/util.o", .flags = flags};
    SmithObject pick_object = {.compiler = "smith-cc", .source = "pick.smith", .output = "build/pick.o", .flags = flags};
    smith_object(b, &main_object);
    smith_object(b, &box_object);
    smith_object(b, &util_object);
    smith_object(b, &pick_object);
    SmithObject twice_object = {.compiler = "smith-cc", .source = "m.smith", .output = "build/m.o", .flags = flags};
    SmithObject users_object = {.compiler = "smith-cc", .source = "users.smith", .output = "build/users.o", .flags = flags};
    smith_object(b, &twice_object);
    smith_object(b, &users_object);
    SmithObject same_object = {.compiler = "smith-cc", .source = "same.smith", .output = "build/same.o", .flags = flags};
    smith_object(b, &same_object);
    SmithObject sliced_object = {.compiler = "smith-cc", .source = "sliced.smith", .output = "build/sliced.o", .flags = flags};
    smith_object(b, &sliced_object);
    SmithProgram program = {.linker = "smith-cc", .output = "build/demo", .objects = (const SmithObject *[]){&main_object, &box_object, &util_object, &pick_object, &twice_object, &users_object, nullptr}};
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

int main() {
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

int main() {
    box::Box b = {2.0f, 3.0f};
    printf("area %f\n", (double)box::area(b));
    box::missing(b);
    return b.nothing;
}
EOF
cat >complete.smith <<'EOF'
module complete;
import geometry::box;

int f() {
    geometry_box_ar
}
EOF
cat >choice.smith <<'EOF'
module choice;
import target;

export i32 pick(i32 x) {
    if constexpr (target::arch == target::aarch64) {
        return x + 1;
    } else {
        return missing_name(x);
    }
}

export i32 never(i32 y) {
    if constexpr (target::os == 0 && target::arch == 0) {
        return y;
    } else if constexpr (y > 1) {
        return 2;
    }
    return 0;
}

export i32 sized() {
    static_assert(target::f32_mantissa_bits == 24);
    return target::f32_mantissa_bits;
}
EOF
cat >pick.smith <<'EOF'
module pick;

// The larger of two values of one type: a's, and b converts to it.
export typeof(a) larger(auto a, typeof(a) b) {
    return a > b ? a : b;
}

// A function known at compile time: each function is an instance.
export typeof(x) apply_twice(constexpr auto f, auto x) {
    return f(f(x));
}

// The arguments that remain: each has its own type.
export usize count_args(auto x, auto... rest) {
    (void)x;
    return countof(rest);
}

// The arguments in a `for constexpr`: the variables are written out, and the editor stays at the lines of this source.
export i32 gathered(auto... rest) {
    i32 total = 100;
    for constexpr (usize i = 0; i < countof(rest); i++) {
        total += (i32)rest[i];
    }
    return total;
}
EOF
cat >genuse.smith <<'EOF'
module genuse;
import pick;

export int f() {
    i32 a = 3;
    i32 b = 8;
    i32 first = pick::larger(a, b);
    f64 second = pick::larger(1.5, 2.5);
    return first + (int)second;
}

static i32 inc(i32 x) {
    return x + 1;
}

export int g() {
    return pick::apply_twice(inc, 4);
}

export usize h() {
    return pick::count_args(1, 2.5, 3);
}

export i32 k() {
    return pick::gathered(5, 6);
}
EOF
cat >tuples.smith <<'EOF'
module tuples;

export typedef struct { i32, f32 } Pair;

export struct { i32, f32 } make(i32 a, f32 b) {
    return {a, b};
}

export f32 total() {
    auto p = make(1, 2.0f);
    p[0] += 1;
    struct { i32, struct { f32, f32 } } n = {1, {2.0f, 3.0f}};
    return p[1] + n[1][0] + (f32)countof(p);
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

# Two functions with slice parameters in a module, and a module that calls the second (issue #133).
cat >m.smith <<'EOF2'
module m;
export i32 a(f32 g[:]) { return (i32)countof(g); }
export i32 b(i32 m[:]) { return (i32)countof(m); }
EOF2
cat >users.smith <<'EOF2'
module users;
import m;

export i32 f(i32 xs[:]) {
    return m::b(xs);
}
EOF2
# A call of an exported function of the same module and a parameter inside countof(m) and m[i] (issue #154).
cat >same.smith <<'EOF2'
module same;
export i32 f(i32 x) { return x; }
export i32 g(i32 m[:]) { return f((i32)countof(m)) + m[0]; }
EOF2
# A slice element in the argument of a generic call (issue #264): the rewrite of `xs[0]` is written again with the call,
# and its tokens must stay the lowering's own, or the editor's token collector never finishes.
cat >sliced.smith <<'EOF2'
module sliced;
import pick;

export i32 h(i32 xs[:]) {
    return pick::larger(xs[0] + 1, 2);
}
EOF2
# The project builds with smith build, and runs.
out=$(smith build 2>&1)
status=$?
expect "the project builds" 0 "$status"
[ "$status" -eq 0 ] || printf '%s\n' "$out" | tail -n 20
expect "the program runs" "area 6.000000" "$(build/demo 2>&1)"
expect "compile_commands.json tells clangd the language of a .smith file" 7 \
    "$(grep -c '"-x"' compile_commands.json)"

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
    'hover main.smith:7:18: function box::area |  | provided by "geometry/box.h" |  | → float |  | Parameters: |  | - box::Box b |  | pure |  | float box::area(box::Box b)'
expect_line "hover: a type, as written" "$log" \
    'hover main.smith:6:10: type-alias box::Box |  | provided by "geometry/box.h" |  | Type: struct box::Box |  | typedef struct box::Box box::Box'
expect_line "definition: the function is in the other module's .smith, at its name" "$log" \
    "definition main.smith:7:18 -> geometry/box.smith:5:12"
expect_line "definition: so is the type" "$log" "definition main.smith:6:10 -> geometry/box.smith:3:50"
expect_line "symbols: main" "$log" "symbol main.smith: main"
expect_line "hover at the definition: the function's name, in the module itself" "$log" \
    "hover geometry/box.smith:5:12: function area |  | → f32 (aka float) |  | Parameters: |  | - Box b |  | pure |  | f32 area(Box b)"
expect_line "hover at the definition: and the type's" "$log" \
    "hover geometry/box.smith:3:50: type-alias Box |  | Type: struct Box |  | typedef struct Box Box"
expect "symbols: a module's names, as written, each once" \
    "symbol geometry/box.smith: (anonymous struct) symbol geometry/box.smith: width symbol geometry/box.smith: height symbol geometry/box.smith: Box symbol geometry/box.smith: area symbol geometry/box.smith: twice symbol geometry/box.smith: perimeter" \
    "$(printf '%s\n' "$log" | grep '^symbol geometry/box.smith' | tr '\n' ' ' | sed 's/ $//')"

# A run-time array `T xs[:]` (issue #106) is shown as the source writes it, `f32[:]`, never as the struct the lowering
# made (`struct smith_array_f32_n`): in the hover of the function, of the parameter and of its use, and in the outline.
cat >geometry/slices.smith <<'EOF'
module geometry::slices;

export f32 pick(f32 xs[:], usize i) {
    return xs[i];
}
EOF
log=$(client open geometry/slices.smith -- hover geometry/slices.smith 3 12 -- hover geometry/slices.smith 3 21 -- \
    hover geometry/slices.smith 4 12 -- symbols geometry/slices.smith)
expect_line "slice: the hover of a function shows its slice parameter as f32[:]" "$log" \
    'hover geometry/slices.smith:3:12: function pick |  | → f32 (aka float) |  | Parameters: |  | - f32[:] xs | - usize i (aka unsigned long) |  | pure |  | f32 pick(f32[:] xs, usize i)'
expect_line "slice: the hover of the parameter shows its type as f32[:]" "$log" \
    'hover geometry/slices.smith:3:21: param xs |  | Type: f32[:] |  | // In pick | f32[:] xs'

# A function after one with a slice parameter (issue #133): the end of the first function's body (its `}` is read while
# the lowering reads the second head) was the slice parameter's, so hover, references and highlights found nothing in
# the second function. Hover, go-to-definition (from a module that calls it) and the outline work on both, on a parameter
# named like the module too.
log=$(client open m.smith users.smith -- hover m.smith 2 12 -- hover m.smith 3 12 -- hover m.smith 3 18 -- \
    references m.smith 3 12 -- definition users.smith 5 15 m.smith:3:12 -- symbols m.smith)
expect_line "after a slice: hover on the first function" "$log" \
    'hover m.smith:2:12: function a |  | → i32 (aka int) |  | Parameters: |  | - f32[:] g |  | pure |  | i32 a(f32[:] g)'
expect_line "after a slice: hover on the second function" "$log" \
    'hover m.smith:3:12: function b |  | → i32 (aka int) |  | Parameters: |  | - i32[:] m |  | pure |  | i32 b(i32[:] m)'
expect "after a slice: hover on its parameter" 1 "$(printf '%s\n' "$log" | grep -c '^hover m.smith:3:18: param m')"
expect "after a slice: references of the second function find it" 1 "$(printf '%s\n' "$log" | grep -c '^reference m.smith:3:12')"
expect_line "after a slice: go-to-definition of the second function, from a call" "$log" \
    "definition users.smith:5:15 -> m.smith:3:12"
expect "after a slice: the outline has both functions" "symbol m.smith: a symbol m.smith: b" \
    "$(printf '%s\n' "$log" | grep '^symbol m.smith' | tr '\n' ' ' | sed 's/ $//')"

# Go-to-definition inside one module (issue #154): a call of an exported function, and a parameter used in countof(m)
# and in m[i], go to the function's name and to the parameter.
log=$(client open same.smith -- definition same.smith 3 33 same.smith:2:12 -- definition same.smith 3 48 same.smith:3:18 -- \
    definition same.smith 3 54 same.smith:3:18)
expect_line "same module: go-to-definition of a call of an exported function" "$log" "definition same.smith:3:33 -> same.smith:2:12"
expect_line "same module: a parameter inside countof(m)" "$log" "definition same.smith:3:48 -> same.smith:3:18"
expect_line "same module: a parameter in m[i]" "$log" "definition same.smith:3:54 -> same.smith:3:18"

# The many-dimensional forms and the qualifier tags are written as the language writes them (the tags of `restrict` and
# `const` slices are declared by hand here: their lowering is the compiler's).
cat >geometry/dims.smith <<'EOF'
module geometry::dims;

struct smith_array_restrict_u8_n { unsigned char * __restrict data; unsigned long count[1]; };
struct smith_array_const_char_n { const char *data; unsigned long count[1]; };

export f32 rows(f32 m[:][2], usize i) {
    return m[i][0];
}

export usize tags(const struct smith_array_restrict_u8_n a, struct smith_array_const_char_n b) {
    return a.count[0] + b.count[0];
}
EOF
cat >geometry/grid.smith <<'EOF'
module geometry::grid;

export f32 grid(f32 g[:][:], usize i) {
    return g[i][0];
}
EOF
log=$(client open geometry/dims.smith -- hover geometry/dims.smith 6 12 -- hover geometry/dims.smith 10 14 -- \
    symbols geometry/dims.smith)
expect_line "dims: the hover of a function shows an f32[:][2] parameter as written" "$log" \
    'hover geometry/dims.smith:6:12: function rows |  | → f32 (aka float) |  | Parameters: |  | - f32[:][2] m | - usize i (aka unsigned long) |  | pure |  | f32 rows(f32[:][2] m, usize i)'
expect_line "dims: the hover shows the tags of qualified slices as written" "$log" \
    'hover geometry/dims.smith:10:14: function tags |  | → usize (aka unsigned long) |  | Parameters: |  | - u8[const restrict :] a | - const char[:] b |  | pure |  | usize tags(u8[const restrict :] a, const char[:] b)'
log=$(client open geometry/grid.smith -- hover geometry/grid.smith 3 12)
expect_line "dims: the hover of a function shows an f32[:][:] parameter as written" "$log" \
    'hover geometry/grid.smith:3:12: function grid |  | → f32 (aka float) |  | Parameters: |  | - f32[:][:] g | - usize i (aka unsigned long) |  | pure |  | f32 grid(f32[:][:] g, usize i)'

# A slice element in a generic call's argument: the editor answers (it used to loop in the token collector, issue #264).
log=$(client open sliced.smith pick.smith -- hover sliced.smith 5 24)
expect_line "slice in a generic call: no diagnostics, and the editor answers" "$log" "diagnostics sliced.smith"

# A call to a generic: the compiler sees an instance there, so the layer answers for the lowering: the hover shows the
# generic as its module wrote it and the instance asked for, and go-to-definition goes to the generic's name.
log=$(client open genuse.smith pick.smith -- hover genuse.smith 7 26 -- definition genuse.smith 7 26 pick.smith:4:18 -- \
    hover genuse.smith 8 26 -- definition genuse.smith 8 26 pick.smith:4:18)
expect_line "generic call: no diagnostics" "$log" "diagnostics genuse.smith"
expect "generic call: and nothing else is reported" 0 "$(printf '%s\n' "$log" | grep -c '^diag genuse.smith')"
expect_line "generic call: hover shows the generic as written and the instance, with the type told from the argument" "$log" \
    'hover genuse.smith:7:26: generic function pick::larger |  | typeof(a) pick::larger(auto a, typeof(a) b) |  | instance pick::larger(i32)'
expect_line "generic call: go-to-definition is the generic's name in its module" "$log" \
    "definition genuse.smith:7:26 -> pick.smith:4:18"
expect_line "generic call: hover, the second instance" "$log" \
    'hover genuse.smith:8:26: generic function pick::larger |  | typeof(a) pick::larger(auto a, typeof(a) b) |  | instance pick::larger(f64)'

# A function passed to `constexpr auto f`: the hover shows the head as written, and the instance names the function.
log=$(client open genuse.smith pick.smith -- hover genuse.smith 17 20 -- definition genuse.smith 17 20 pick.smith:9:18)
expect_line "generic function parameter: hover shows the head as written and the function in the instance" "$log" \
    'hover genuse.smith:17:20: generic function pick::apply_twice |  | typeof(x) pick::apply_twice(constexpr auto f, auto x) |  | instance pick::apply_twice(inc, int)'
expect_line "generic function parameter: go-to-definition is the generic's name" "$log" \
    "definition genuse.smith:17:20 -> pick.smith:9:18"
expect "generic function parameter: no diagnostics" 0 "$(printf '%s\n' "$log" | grep -c '^diag genuse.smith')"

# `auto... rest`: the head as written, and the instance lists the type of each argument.
log=$(client open genuse.smith pick.smith -- hover genuse.smith 21 20 -- definition genuse.smith 21 20 pick.smith:14:14)
expect_line "generic argument list: hover shows the head as written and every type in the instance" "$log" \
    'hover genuse.smith:21:20: generic function pick::count_args |  | usize pick::count_args(auto x, auto... rest) |  | instance pick::count_args(int, f64, int)'
expect_line "generic argument list: go-to-definition is the generic's name" "$log" \
    "definition genuse.smith:21:20 -> pick.smith:14:14"
expect "generic argument list: no diagnostics" 0 "$(printf '%s\n' "$log" | grep -c '^diag genuse.smith')"

# The argument list and `for constexpr` in the generic: the head as written, the instance's types, and no diagnostic in the unrolled body.
log=$(client open genuse.smith pick.smith -- hover genuse.smith 25 20 -- definition genuse.smith 25 20 pick.smith:20:12)
expect_line "generic argument list and for constexpr: hover shows the head as written and the instance" "$log" \
    'hover genuse.smith:25:20: generic function pick::gathered |  | i32 pick::gathered(auto... rest) |  | instance pick::gathered(int, int)'
expect_line "generic argument list and for constexpr: go-to-definition is the generic's name" "$log" \
    "definition genuse.smith:25:20 -> pick.smith:20:12"
expect "generic argument list and for constexpr: no diagnostics" 0 "$(printf '%s\n' "$log" | grep -c '^diag ')"

# `if constexpr` outside a generic: the branch taken is ordinary code (hover works in it), the branch not taken is never
# given to the parser, so an undeclared name there is no diagnostic and hover there is empty (as in a generic's instance
# that was not made). A condition that is no constant is reported at the condition. The `target::` facts are
# literals to the compiler.
log=$(client open choice.smith -- hover choice.smith 6 16 -- hover choice.smith 8 16)
expect_line "if constexpr: the branch not taken has no diagnostics" "$log" "diagnostics choice.smith"
expect "if constexpr: ... of any kind, the undeclared name included" 0 \
    "$(printf '%s\n' "$log" | grep -c '^diag choice.smith:8')"
expect "if constexpr: a name in the branch taken is hovered as in any code" 1 \
    "$(printf '%s\n' "$log" | grep -c '^hover choice.smith:6:16: param x')"
expect_line "if constexpr: a name in the branch not taken has no hover" "$log" "hover choice.smith:8:16: (nothing)"
expect "if constexpr: a condition that is no constant is reported at the condition, as written" 1 \
    "$(printf '%s\n' "$log" | grep -c '^diag choice.smith:15:26: In `if constexpr`: `y` is not a constant')"

# A tuple, `struct { i32, f32 }`, `t[0]` and `countof(t)`: no diagnostics, and a hover on the member finds its type.
log=$(client open tuples.smith -- hover tuples.smith 10 14)
expect_line "tuples: no diagnostics" "$log" "diagnostics tuples.smith"
expect "tuples: ... of any kind" 0 "$(printf '%s\n' "$log" | grep -c '^diag ')"

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

# Queries from the shell (issue #248): `smith lsp def|refs|hover|diag|symbols` and `smith lsp -`, answered from the same
# server and index as the editor, one `path:line:col: kind: text` line for each fact. A project of two modules, an alias,
# a generic, a C file that calls a module by its C name, and a C-only folder.
mkdir -p "$work/q/geom" "$work/qc"
cd "$work/q" || exit 1
cat >build.c <<'EOF'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu2y", "-Wall", "-Wextra", "-I", ".smith/include", nullptr};
    SmithObject vec = {.compiler = "smith-cc", .source = "geom/vec.smith", .output = "build/vec.o", .flags = flags};
    SmithObject user = {.compiler = "smith-cc", .source = "user.smith", .output = "build/user.o", .flags = flags};
    SmithObject c_object = {.compiler = "smith-cc", .source = "c_use.c", .output = "build/c_use.o", .flags = flags};
    smith_object(b, &vec);
    smith_object(b, &user);
    smith_object(b, &c_object);
    SmithProgram program = {.linker = "smith-cc", .output = "build/demo", .objects = (const SmithObject *[]){&vec, &user, &c_object, nullptr}};
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    smith_compile_commands(b, "compile_commands.json");
}
EOF
cat >geom/vec.smith <<'EOF'
module geom::vec;

export typedef struct {
    f32 x;
    f32 y;
} Vec;

export f32 dot(Vec a, Vec b) {
    return a.x * b.x + a.y * b.y;
}

export typeof(a) larger(auto a, typeof(a) b) {
    return a > b ? a : b;
}

[[smith::c_name("geom_vec_squared")]] export f32 length2(Vec a) {
    return a.x * a.x;
}
EOF
cat >user.smith <<'EOF'
module user;

import geom::vec as v;
import geom::vec;

export f32 length_squared(v::Vec a) {
    auto n = v::dot(a, a);
    return n;
}

export f32 biggest(vec::Vec a, vec::Vec b) {
    return vec::larger(vec::dot(a, a), vec::dot(b, b));
}

export f32 squared(vec::Vec a) {
    return vec::length2(a);
}
EOF
cat >c_use.c <<'EOF'
#include "geom/vec.h"

float c_use(geom_vec_Vec a) {
    return geom_vec_dot(a, a);
}

int main(void) {
    return 0;
}
EOF
if ! smith build >build.log 2>&1; then
    echo "FAIL  query: the scratch project does not build"
    head -5 build.log
    failures=$((failures + 1))
fi
query() { # arguments of `smith lsp`; the answer, the paths relative to the folder
    smith lsp "$@" 2>/dev/null
}
expect "query: the first query waits for the index and says so on stderr (never an answer from an unfinished index)" \
    "smith lsp: bringing the index up to date" "$(smith lsp def vec::dot 2>&1 >/dev/null | head -n 1)"
expect "query def: a module's function, as written, with its declaration" \
    "geom/vec.smith:8:12: function: geom::vec::dot: export f32 dot(Vec a, Vec b) {" "$(query def vec::dot)"
expect "query def: a bare name is every module's, and each line says which" \
    "geom/vec.smith:8:12: function: geom::vec::dot: export f32 dot(Vec a, Vec b) {" "$(query def dot)"
expect "query def: a C name" "geom/vec.smith:8:12: function: geom_vec_dot: export f32 dot(Vec a, Vec b) {" \
    "$(query def geom_vec_dot)"
expect "query refs: every spelling (an alias, the module's own name, the C name in a C file) of a function" \
    "c_use.c:4:12: ref: return geom_vec_dot(a, a);
geom/vec.smith:8:12: ref: export f32 dot(Vec a, Vec b) {
user.smith:7:17: ref: auto n = v::dot(a, a);
user.smith:12:29: ref: return vec::larger(vec::dot(a, a), vec::dot(b, b));
user.smith:12:45: ref: return vec::larger(vec::dot(a, a), vec::dot(b, b));" "$(query refs vec::dot)"
expect "query refs: a call of a generic is a reference to the generic" \
    "user.smith:12:17: ref: return vec::larger(vec::dot(a, a), vec::dot(b, b));" "$(query refs vec::larger)"
# A function with a C name is in the index under it: the written name finds it all the same (issue #248, the review).
expect "query refs: a function with a c_name, asked by its written name" \
    "geom/vec.smith:16:50: ref: [[smith::c_name(\"geom_vec_squared\")]] export f32 length2(Vec a) {
user.smith:16:17: ref: return vec::length2(a);" "$(query refs vec::length2)"
hover=$(query hover user.smith:7:10)
case $hover in *"user.smith:7:10: hover: "*"float"*) hover=yes ;; *) hover=no ;; esac
expect "query hover: the type of an auto local, as written" yes "$hover"
expect "query symbols: the outline of a module" \
    "geom/vec.smith:3:16: class: (anonymous struct)
geom/vec.smith:4:5: field: x
geom/vec.smith:5:5: field: y
geom/vec.smith:3:8: class: Vec
geom/vec.smith:8:8: function: dot
geom/vec.smith:12:18: function: larger
geom/vec.smith:16:46: function: length2" "$(query symbols geom/vec.smith)"
expect "query diag: no error, no output" "" "$(query diag user.smith)"
smith lsp diag user.smith >/dev/null 2>&1
expect "query diag: and exit status 0" 0 $?
cp user.smith user.smith.keep
printf 'void f(void) { printf("x"); }\n' >>user.smith
diag=$(query diag user.smith)
case $diag in *"user.smith:18:16: error: "*"printf"*) diag=yes ;; *) diag=no ;; esac
expect "query diag: an edit made outside the editor is an error at its place" yes "$diag"
smith lsp diag user.smith >/dev/null 2>&1
expect "query diag: and exit status 1" 1 $?
cp user.smith.keep user.smith
expect "query batch: queries one a line from the standard input, answered in order" \
    "geom/vec.smith:8:12: function: geom::vec::dot: export f32 dot(Vec a, Vec b) {
user.smith:12:17: ref: return vec::larger(vec::dot(a, a), vec::dot(b, b));" \
    "$(printf 'def vec::dot\nrefs vec::larger\n' | smith lsp - 2>/dev/null)"
cd "$work/qc" || exit 1
cat >c_only.c <<'EOF'
int twice(int x) {
    return x * 2;
}

int four(void) {
    return twice(2) + twice(2);
}
EOF
printf '[{"directory": "%s", "file": "c_only.c", "command": "cc -c c_only.c"}]\n' "$(pwd -P)" >compile_commands.json
expect "query C-only: def, refs and symbols of C names" \
    "c_only.c:1:5: function: twice: int twice(int x) {
c_only.c:1:5: ref: int twice(int x) {
c_only.c:6:12: ref: return twice(2) + twice(2);
c_only.c:6:23: ref: return twice(2) + twice(2);
c_only.c:1:1: function: twice
c_only.c:5:1: function: four" \
    "$(printf 'def twice\nrefs twice\nsymbols c_only.c\n' | smith lsp - 2>/dev/null)"
cd "$work/p" || exit 1

fi
if [ "$part" != project ]; then
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

int main() {
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

i32 g() {
    return foo::;
}
EOF
cat >modules/declared.smith <<'EOF'
module declared;

[[smith::effects(alloc)]] i32 grab(i32 x) {
    return x;
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
    'hover main.smith:6:18: function foo::f |  | provided by "foo.smith" |  | → int32_t (aka int) |  | Parameters: |  | - int32_t x (aka int) |  | pure |  | int32_t foo::f(int32_t x)'
expect_line "modules: go-to-definition lands in the other module's source" "$log" \
    "definition main.smith:6:18 -> foo.smith:3:12"
expect_line "modules: hover at the function's definition" "$log" \
    "hover foo.smith:3:12: function f |  | → i32 (aka int) |  | Parameters: |  | - i32 x (aka int) |  | pure |  | i32 f(i32 x)"
expect_line "modules: hover at the type's definition" "$log" \
    "hover foo.smith:7:41: type-alias Pair |  | Type: struct Pair |  | typedef struct Pair Pair"
expect "modules: symbols, as written, each once" \
    "symbol foo.smith: f symbol foo.smith: (anonymous struct) symbol foo.smith: a symbol foo.smith: b symbol foo.smith: Pair symbol foo.smith: sum" \
    "$(printf '%s\n' "$log" | grep '^symbol ' | tr '\n' ' ' | sed 's/ $//')"
expect "modules: references of the function: its definition and its call" \
    "reference foo.smith:3:12 reference main.smith:6:18" \
    "$(printf '%s\n' "$log" | grep '^reference ' | tr '\n' ' ' | sed 's/ $//')"
expect "modules: highlights of it: the name once" "highlight 3:12" "$(printf '%s\n' "$log" | grep '^highlight ' | tr '\n' ' ' | sed 's/ $//')"
# A function with a declared set: the hover says so. clangd 23 prints an "Annotation:" line for some AnnotateAttrs; the
# declared set's own storage (smith::effects:...) does not show on r10, as this line checks. If a later LLVM shows it,
# patches/pending/clangd-hidden-annotations.patch (waiting for r11) is the way to hide it.
log=$(client_in "$modules" open declared.smith -- hover declared.smith 3 31)
expect_line "modules: hover on a function with a declared set" "$log" \
    "hover declared.smith:3:31: function grab |  | → i32 (aka int) |  | Parameters: |  | - i32 x (aka int) |  | effects: alloc (declared) |  | static i32 grab(i32 x)"
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


# A project with only .smith/compile_commands.json (issue #43): clangd reads that one, so the define its command has
# reaches the source; a root compile_commands.json, when there is one, is clangd's own choice and wins.
mkdir -p smithdir/.smith rootwins/.smith
for dir in smithdir rootwins; do
    printf '#ifndef NEEDED\n#error needed\n#endif\nint f(void) { return NEEDED; }\n' >"$dir/needs.c"
done
printf '[{"directory": "%s", "command": "smith-cc -c needs.c -o needs.o -DNEEDED=1", "file": "needs.c"}]\n' \
    "$work/p/smithdir" >smithdir/.smith/compile_commands.json
printf '[{"directory": "%s", "command": "smith-cc -c needs.c -o needs.o -DNEEDED=1", "file": "needs.c"}]\n' \
    "$work/p/rootwins" >rootwins/.smith/compile_commands.json
printf '[{"directory": "%s", "command": "smith-cc -c needs.c -o needs.o", "file": "needs.c"}]\n' \
    "$work/p/rootwins" >rootwins/compile_commands.json
log=$(client_in "$(cd smithdir && pwd -P)" open needs.c)
expect_line "smith dir: .smith/compile_commands.json is the project's compile commands (no #error)" "$log" "diagnostics needs.c"
expect "smith dir: ... and nothing is reported" 0 "$(printf '%s\n' "$log" | grep -c '^diag ')"
log=$(client_in "$(cd rootwins && pwd -P)" open needs.c)
expect "smith dir: a root compile_commands.json wins (the #error is reported)" 1 \
    "$(printf '%s\n' "$log" | grep -c '^diag needs.c:2:2: Needed')"
# smith's defaults in the editor (issue #43): a project with no .clang-format is formatted in smith's style, and one
# with no .clang-tidy is checked by smith's checks; a project's own file replaces smith's whole.
cd "$(cd "$editor_work" && pwd -P)" || exit 1
mkdir -p editor-none editor-own
for dir in editor-none editor-own; do
    printf 'int f(int x){return x?f(x-1):0;}\n' >"$dir/f.c"
done
printf 'BasedOnStyle: LLVM\nIndentWidth: 3\nAllowShortFunctionsOnASingleLine: None\n' >editor-own/.clang-format
printf 'Checks: "-*,readability-function-size"\n' >editor-own/.clang-tidy
# The edits are small ones: the one that starts a line is a newline and the indent.
log=$(client_in "$(cd editor-none && pwd -P)" open f.c -- format f.c)
expect "editor: with no .clang-format, smith's style (an indent of 4)" 1 \
    "$(printf '%s\n' "$log" | grep -Fxc 'format-edit \n    ')"
expect "editor: with no .clang-tidy, smith's checks report (misc-no-recursion)" 1 \
    "$(printf '%s\n' "$log" | grep -c "^diag f.c:1:5: Function 'f' is within a recursive call chain")"
log=$(client_in "$(cd editor-own && pwd -P)" open f.c -- format f.c)
expect "editor: a project's .clang-format replaces smith's (an indent of 3)" 1 \
    "$(printf '%s\n' "$log" | grep -Fxc 'format-edit \n   ')"
expect "editor: ... and none of smith's indent" 0 "$(printf '%s\n' "$log" | grep -Fxc 'format-edit \n    ')"
expect "editor: a project's .clang-tidy replaces smith's (no recursion finding)" 0 \
    "$(printf '%s\n' "$log" | grep -c '^diag .*recursive')"

# smith's naming check judges a .smith file's names as written (`Box`, not `m_Box`), in the editor as in `smith lint`: a
# well-named typedef and enum are quiet, a badly named typedef is flagged at the written name.
mkdir -p editor-smith
cat >editor-smith/m.smith <<'SMITH'
module m;

export typedef struct {
    u32 a;
} Box;

export typedef enum {
    Mode_Fast,
    Mode_Slow,
} Mode;

export typedef struct {
    u32 a;
} bad_type;
SMITH
log=$(client_in "$(cd editor-smith && pwd -P)" open m.smith)
expect "editor: a .smith typedef is judged as written (only bad_type is flagged)" \
    "diag m.smith:14:3: Invalid case style for typedef 'bad_type' (fix available)" "$(printf '%s\n' "$log" | grep '^diag ')"

# smith-effects (issue #45) is one of smith's default checks, so the editor shows it too: a raw call in a .smith file is
# reported at the `c::` where the source wrote it, with the replacement named; a call that is pure is quiet.
mkdir -p editor-effects
cat >editor-effects/fx.smith <<'SMITH'
module fx;

import <stdio.h>;
import <string.h>;

usize measure(const char *text) {
    return c::strlen(text);
}

void say() {
    c::puts("hello");
}
SMITH
log=$(client_in "$(cd editor-effects && pwd -P)" open fx.smith)
expect "editor: smith-effects reports a raw call in a .smith file, as written, and a pure call is quiet" \
    "diag fx.smith:11:5: \`c::puts\` touches the world: take a \`SmithConsole *\` from main's SmithIo and print and read through it (smith_console_print, smith_console_read_line)" \
    "$(printf '%s\n' "$log" | grep '^diag ' | sed 's/ (fix available)$//')"

# A payload enum (issue #198): clangd's token buffer must account for every token the lowering writes for it, or it spins
# forever building the buffer (#205). The file is read, and an error of the dot rule is reported where it was written.
mkdir -p editor-enum
cat >editor-enum/shapes.smith <<'SMITH'
module shapes;

typedef struct {
    f32 radius;
} Circle;

typedef enum {
    Circle circle;
    empty;
} Shape;

Shape none() {
    return .nothing;
}
SMITH
log=$(client_in "$(cd editor-enum && pwd -P)" open shapes.smith)
expect_line "editor: a file with a payload enum is read, and the dot rule's error is at the written place" "$log" \
    "diag shapes.smith:13:12: \`Shape\` has no variant \`nothing\`"
expect "editor: and the names the lowering writes for the enum (its tag and constants) are not judged" 0 \
    "$(printf '%s\n' "$log" | grep -c 'Invalid case style')"

# Effects across files, in the editor (issue #246): a.c's pure function calls noisy, which b.c defines and prints in. The
# summary of b.c is the build's (made here by the compile), the editor reads it, and a.c shows the link's error with the
# chain. Saving b.c without the print rewrites its summary and a.c's diagnostics are published again without the error,
# with a.c untouched.
mkdir -p "$work/fx/.smith"
fx=$(cd "$work/fx" && pwd -P)
cat >"$fx/lib.h" <<'EOF2'
int noisy(int x);
EOF2
cat >"$fx/a.c" <<'EOF2'
#include "lib.h"
[[smith::pure]] int bad(int x) { return noisy(x); }
int main(void) { return bad(1); }
EOF2
cat >"$fx/b.c" <<'EOF2'
#include <stdio.h>
#include "lib.h"
int noisy(int x) {
    printf("%d\n", x);
    return x;
}
EOF2
cat >"$fx/.smith/compile_commands.json" <<EOF2
[{"directory":"$fx","file":"a.c","output":"a.o","arguments":["smith-cc","-std=gnu2y","-c","a.c","-o","a.o"]},
 {"directory":"$fx","file":"b.c","output":"b.o","arguments":["smith-cc","-std=gnu2y","-c","b.c","-o","b.o"]}]
EOF2
cp "$fx/.smith/compile_commands.json" "$fx/compile_commands.json"
(cd "$fx" && smith cc -std=gnu2y -c b.c -o b.o) >/dev/null 2>&1
log=$(client_in "$fx" open a.c b.c -- replace b.c '    printf("%d\n", x);
' '' -- save b.c)
expect_line "effects: a.c shows the error through b.c, with the chain" "$log" \
    "diag a.c:2:41: Smith-effect: \`bad\` is [[smith::pure]], but it has the effects console: bad -> noisy (a.c:2) -> printf (b.c:4)"
expect "effects: after b.c is saved without the print, a.c has no diagnostics again" 1 \
    "$(printf '%s\n' "$log" | grep -c '^diag a.c')"

fi
echo
echo "$total language server checks, $failures failed"
[ "$failures" -eq 0 ]

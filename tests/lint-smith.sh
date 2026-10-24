#!/bin/sh
# `smith lint` on .smith files: clang-tidy run by smith reads them as `smith cc` does (the token hook, and `-x c` with
# `-std=gnu2y`), shows what it finds as the source wrote it, at the source's lines and columns, and judges the names of
# a module as written (`area`, `Box`), never as the lowering makes them (`geometry_box_area`). A scratch project with the
# template's .clang-tidy: a module and its importer with real findings in both, a clean module, the fixes, a compile
# error of the lowering's, and plain C beside .smith in one run. A smith without clang inside (the stage0), or whose
# clang lacks the token hook, is skipped.
# Usage: sh tests/lint-smith.sh path/to/smith
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

mkdir -p "$work/p/geometry" "$work/p/clean"
cd "$work/p" || exit 1
# smith-module has its own tests below; the files here are plain or modules as each case needs.
grep -v '^  smith-module,$' "$here/templates/defaults/clang-tidy" >.clang-tidy
cat >geometry/box.smith <<'EOF'
module geometry::box;

export typedef struct {
    f32 width;
    f32 height;
} Box;

export typedef struct {
    f32 w;
} bad_Type;

export f32 area(Box b) {
    f32 half = 1;
    return b.width * b.height * half;
}

export f32 Perimeter(Box b) {
    return 2 * (b.width + b.height);
}

static i32 bad_Helper(i32 x) {
    i32 Bad_Name = x;
    return Bad_Name;
}

export i32 use_helper(i32 x) {
    return bad_Helper(x);
}
EOF
cat >main.smith <<'EOF'
import geometry::box;

i32 main(void) {
    box::Box b = {2, 3};
    i32 BadLocal = 4;
    return (i32)box::area(b) + BadLocal;
}
EOF
# A module whose names all follow the template's rules, with the sized types the lowering makes: nothing to report.
cat >clean/ok.smith <<'EOF'
module clean::ok;

export typedef struct {
    u32 count;
    f64 total;
} Tally;

export typedef enum {
    Mode_Fast,
    Mode_Slow,
} Mode;

export constexpr u32 limit = 4;

export f64 mean(Tally t) {
    return t.total / (f64)t.count;
}

export Mode pick(i32 n) {
    return n > (i32)limit ? Mode_Slow : Mode_Fast;
}
EOF
cat >plain.c <<'EOF'
int geometry_box_Odd(int Bad_Arg) {
    return Bad_Arg;
}
EOF
cat >plain2.c <<'EOF'
int geometry_box_Area(void) {
    return 0;
}
EOF

# The findings of a run, one line each, as the tool prints them (the rest of its output is the carets and the fixes).
findings() {
    grep -E '^/.*:[0-9]+:[0-9]+: (error|warning):' | sed "s|^$work/p/||"
}
flags="-I. -I.smith/include"
run() { # files...
    smith lint --quiet "$@" -- $flags 2>&1
}

# --- a module and its importer, in memory (no generated header yet) ------------------------------------------------------
expected="geometry/box.smith:10:3: error: invalid case style for typedef 'bad_Type' [readability-identifier-naming,-warnings-as-errors]
geometry/box.smith:17:12: error: invalid case style for function 'Perimeter' [readability-identifier-naming,-warnings-as-errors]
geometry/box.smith:21:12: error: invalid case style for function 'bad_Helper' [readability-identifier-naming,-warnings-as-errors]
geometry/box.smith:22:9: error: invalid case style for variable 'Bad_Name' [readability-identifier-naming,-warnings-as-errors]
main.smith:5:9: error: invalid case style for variable 'BadLocal' [readability-identifier-naming,-warnings-as-errors]"
run geometry/box.smith main.smith >"$work/in-memory"
expect "findings are at the source's lines, with the names as written" "$expected" "$(findings <"$work/in-memory")"
expect "the lowering's names never appear in a finding" "0" "$(grep -c 'geometry_box_' "$work/in-memory")"
expect "the importer reports nothing of the module's names" "0" "$(findings <"$work/in-memory" | grep -c '^main.smith.*\(area\|Box\)')"

# --- with the headers `smith build` writes -------------------------------------------------------------------------------
mkdir -p .smith/include/geometry
smith cc --smith-header geometry/box.smith -o .smith/include/geometry/box.h
run geometry/box.smith main.smith >"$work/headers"
expect "the same findings with the generated header on disk" "$expected" "$(findings <"$work/headers")"
expect "the lowering's names never appear with the header on disk" "0" "$(grep -c 'geometry_box_' "$work/headers")"
run main.smith >"$work/importer"
expect "an importer alone reports its own and nothing in the module's header" "main.smith:5:9: error: invalid case style for variable 'BadLocal' [readability-identifier-naming,-warnings-as-errors]" \
    "$(findings <"$work/importer")"

# --- the carets and the fixes are the source's --------------------------------------------------------------------------
run geometry/box.smith >"$work/carets"
expect "a finding shows the source line, its fix at the written name" "   17 | export f32 Perimeter(Box b) {
      |            ^~~~~~~~~
      |            perimeter" "$(grep -A2 ' 17 | export f32' "$work/carets")"
cp -R "$work/p" "$work/fix" && cd "$work/fix" || exit 1
run --fix geometry/box.smith main.smith >/dev/null
expect "--fix rewrites the written names in the source" "$(cd "$work/p" && sed -e 's/} bad_Type;/} BadType;/' -e 's/Perimeter/perimeter/' \
    -e 's/bad_Helper/bad_helper/g' -e 's/Bad_Name/bad_name/g' geometry/box.smith)" "$(cat geometry/box.smith)"
expect "--fix rewrites an importer's own names" "$(sed 's/BadLocal/bad_local/g' "$work/p/main.smith")" "$(cat main.smith)"
cd "$work/p" || exit 1

# --- any check names things as written -----------------------------------------------------------------------------------
cat >geometry/recursive.smith <<'EOF'
module geometry::recursive;

export u32 fact(u32 n) {
    return n < 2 ? 1 : n * fact(n - 1);
}
EOF
smith lint --quiet --checks='-*,misc-no-recursion' geometry/recursive.smith -- $flags >"$work/recursion" 2>&1
expect "another check's message has the written name too" "geometry/recursive.smith:3:12: error: function 'fact' is within a recursive call chain [misc-no-recursion,-warnings-as-errors]" \
    "$(findings <"$work/recursion" | head -n 1)"
expect "and its note too" "geometry/recursive.smith:3:12: note: example recursive call chain, starting from function 'fact'" \
    "$(sed "s|$work/p/||" "$work/recursion" | grep 'note:.*example recursive' | head -n 1)"

# --- every kind of name the lowering renames is judged as written ---------------------------------------------------------
# A typedef, a struct tag, an enumerator, a constant and a parameter, each well named (quiet) and badly named (flagged at
# the written name, never the lowering's `kinds_bad_type`).
cat >kinds.smith <<'SMITH'
module kinds;

export typedef struct {
    u32 a;
} Good;

export typedef struct {
    u32 a;
} bad_type;

export struct Tag {
    u32 a;
};

export struct tag_bad {
    u32 a;
};

export typedef enum {
    Color_Red,
    Color_Green,
} Color;

export typedef enum {
    Shade_Light,
    bad_shade,
} Shade;

export constexpr u32 limit = 4;
export constexpr u32 BadLimit = 4;

export u32 f(u32 good_param, u32 BadParam) {
    return good_param + BadParam + limit + BadLimit;
}
SMITH
run kinds.smith >"$work/kinds"
expect "typedef, tag, enumerator, constant and parameter: the bad ones flagged as written, the good ones quiet" \
    "kinds.smith:9:3: error: invalid case style for typedef 'bad_type' [readability-identifier-naming,-warnings-as-errors]
kinds.smith:15:15: error: invalid case style for struct 'tag_bad' [readability-identifier-naming,-warnings-as-errors]
kinds.smith:26:5: error: invalid case style for enum constant 'bad_shade'; cannot be fixed automatically [readability-identifier-naming,-warnings-as-errors]
kinds.smith:30:22: error: invalid case style for constexpr variable 'BadLimit' [readability-identifier-naming,-warnings-as-errors]
kinds.smith:32:34: error: invalid case style for parameter 'BadParam' [readability-identifier-naming,-warnings-as-errors]" \
    "$(findings <"$work/kinds")"
expect "... and the lowering's names never appear" "0" "$(grep -c 'kinds_' "$work/kinds")"

# --- a clean module -------------------------------------------------------------------------------------------------------
run clean/ok.smith >"$work/clean"
status=$?
expect "a module that follows the rules, sized types and all, reports nothing" "" "$(findings <"$work/clean")"
expect "its exit status is 0" "0" "$status"

# --- the lowering's own error, and clang's, through lint ----------------------------------------------------------------
cat >bad_call.smith <<'EOF'
import <stdio.h>;

int main(void) {
    printf("hello\n");
    return 0;
}
EOF
run bad_call.smith >"$work/errors"
expect "the lowering's error is at the source, as written" "bad_call.smith:4:5: error: \`printf\` is a C name; write \`c::printf\` [clang-diagnostic-error]" \
    "$(findings <"$work/errors" | grep clang-diagnostic | head -n 1)"

# --- plain C is as it was ------------------------------------------------------------------------------------------------
run plain.c >"$work/plain-alone"
plain="plain.c:1:5: error: invalid case style for function 'geometry_box_Odd' [readability-identifier-naming,-warnings-as-errors]
plain.c:1:26: error: invalid case style for parameter 'Bad_Arg' [readability-identifier-naming,-warnings-as-errors]"
expect "a C file's names are its own, underscores or not" "$plain" "$(findings <"$work/plain-alone")"
run main.smith plain.c >"$work/mixed"
expect "plain C beside a .smith file in one run is reported the same" "main.smith:5:9: error: invalid case style for variable 'BadLocal' [readability-identifier-naming,-warnings-as-errors]
$plain" "$(findings <"$work/mixed")"
run plain.c geometry/box.smith plain2.c >"$work/around"
expect "plain C on both sides of a .smith file keeps its names" "$(printf '%s\n%s\n%s\n' "$plain" "$(printf '%s\n' "$expected" | grep '^geometry')" \
    "plain2.c:1:5: error: invalid case style for function 'geometry_box_Area' [readability-identifier-naming,-warnings-as-errors]" | sort)" \
    "$(findings <"$work/around" | sort)"

# --- `smith lint` with no arguments lints the .smith sources a build compiles --------------------------------------------
mkdir -p "$work/q" && cd "$work/q" || exit 1
cp "$here/templates/defaults/clang-tidy" .clang-tidy
cat >build.c <<'EOF'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject object = {.compiler = "smith-cc", .source = "main.smith", .output = "build/main.o"};
    smith_object(b, &object);
    smith_default(b, object.step.outputs);
}
EOF
cat >main.smith <<'EOF'
int main(void) {
    i32 BadLocal = 0;
    return BadLocal;
}
EOF
smith lint >"$work/verb.out" 2>"$work/verb.err"
status=$?
expect "smith lint with no arguments fails on a finding in a .smith file the build compiles" "1" "$status"
expect "... and reports it at the source, as written" "main.smith:2:9: error: invalid case style for variable 'BadLocal' [readability-identifier-naming,-warnings-as-errors]" \
    "$(grep -h 'main.smith:2:9' "$work/verb.out" "$work/verb.err" | head -n 1 | sed 's|^.*/main.smith|main.smith|')"
cd "$work/p" || exit 1

# --- a call to a generic: the instance is judged as the generic's name, not `push_array<f64>` ---------------------------
mkdir -p "$work/p/generic" "$work/p/smith"
cat >"$work/p/smith/pool.smith" <<'EOF2'
module smith::pool;

export typedef struct {
    u8 *base;
    usize used;
} Pool;

export T *push_array(Pool *pool, constexpr type T, usize count) {
    T *result = (T *)(pool->base + pool->used);
    pool->used += sizeof(T) * count;
    return result;
}
EOF2
cat >"$work/p/generic/user.smith" <<'EOF2'
module generic::user;
import smith::pool;

export f64 *fill(pool::Pool *pool, usize count) {
    f64 *values = pool::push_array(pool, f64, count);
    for (usize i = 0; i < count; i++)
        values[i] = 0;
    return values;
}
EOF2
run generic/user.smith smith/pool.smith >"$work/generic.out"
echo "$?" >"$work/generic.status"
expect "a module that calls a generic lints clean" "0" "$(cat "$work/generic.status")"
expect "... with no finding about the instance's name" "0" "$(grep -c "push_array<" "$work/generic.out")"

# --- tuples (issue #96): the struct a tuple lowers to and its fields `_0`, `_1` are named by the lowering, not the program -
mkdir -p "$work/p/tuples"
cat >"$work/p/tuples/pairs.smith" <<'EOF2'
module tuples::pairs;

export typedef struct { i32, f32 } Pair;

export struct { i32, struct { f32, f32 } } nested() {
    return {1, {2.0f, 3.0f}};
}

export Pair make(i32 a, f32 b) {
    return {a, b};
}

export f32 total(struct { i32, f32 } t, Pair *p) {
    (*p)[0] += t[0];
    return t[1] + (f32)countof(t);
}
EOF2
cat >"$work/p/tuples/user.smith" <<'EOF2'
module tuples::user;
import tuples::pairs;

export f32 use() {
    auto p = pairs::make(1, 2.0f);
    auto n = pairs::nested();
    return pairs::total(p, &p) + n[1][0];
}
EOF2
run tuples/pairs.smith tuples/user.smith >"$work/tuples.out"
echo "$?" >"$work/tuples.status"
expect "a module with tuples lints clean" "0" "$(cat "$work/tuples.status")"
expect "... with no finding about the struct or the fields the lowering made" "0" \
    "$(grep -c "smith_tuple_\|'_[0-9]'" "$work/tuples.out")"

# --- smith-preprocessor (issue #46): a .smith file has no preprocessor ----------------------------------------------------
mkdir -p "$work/p/pre"
cat >"$work/p/pre/bad.smith" <<'EOF2'
module pre::bad;

#include <stdio.h>
import <assert.h>;
import <math.h>;
import <stdint.h>;
#define LIMIT 4
#pragma STDC FP_CONTRACT OFF

#if 1
export i32 f(i32 x) {
    c::assert(x < c::SIZE_MAX);
    c::assert(c::isfinite(1.0));
    _Pragma("clang diagnostic push")
    return x + LIMIT;
}
#else
#endif
#error nope
EOF2
cat >"$work/p/pre/optout.smith" <<'EOF2'
module pre::optout;

#define LIMIT 4 // NOLINT(smith-preprocessor)

export i32 g(void) {
    return LIMIT;
}
EOF2
cat >"$work/p/pre/plain.c" <<'EOF2'
#include <stdio.h>
#define LIMIT 4
#if LIMIT
int plain(void) { return LIMIT; }
#endif
EOF2
cat >"$work/p/pre/good.smith" <<'EOF2'
module pre::good;

import target;
import smith::contract;
import smith::math;

constexpr usize limit = 4;

export usize h(usize x) {
    contract::assert(x < math::max(usize));
    if constexpr (target::os == target::macos) {
        return x + limit;
    } else {
        return x;
    }
}
EOF2
run pre/bad.smith >"$work/pre.out"
preprocessor_only() { findings | grep -E "smith-preprocessor" | sed -e 's/ \[smith-preprocessor,-warnings-as-errors\]$//' -e 's/: \(`[^`]*`\) is the preprocessor, and a .smith file has none: .*/: \1/' -e 's/: \(`c::[a-zA-Z_]*`\) is a C macro, and a .smith file has no preprocessor: .*/: \1 macro/'; }
expected="pre/bad.smith:3:1: error: \`#include\`
pre/bad.smith:7:1: error: \`#define\`
pre/bad.smith:8:1: error: \`#pragma\`
pre/bad.smith:10:1: error: \`#if\`
pre/bad.smith:12:8: error: \`c::assert\` macro
pre/bad.smith:12:22: error: \`c::SIZE_MAX\` macro
pre/bad.smith:13:8: error: \`c::assert\` macro
pre/bad.smith:13:18: error: \`c::isfinite\` macro
pre/bad.smith:14:5: error: \`_Pragma\`
pre/bad.smith:17:1: error: \`#else\`
pre/bad.smith:18:1: error: \`#endif\`
pre/bad.smith:19:1: error: \`#error\`"
expect "smith-preprocessor: every directive, _Pragma and C macro of a .smith file, at its place" "$expected" "$(preprocessor_only <"$work/pre.out")"
expect "smith-preprocessor: #include says import" "1" "$(grep -c 'write `import <header>;`' "$work/pre.out")"
expect "smith-preprocessor: #define says constexpr" "1" "$(grep -c 'a constant is `constexpr usize name = value;`' "$work/pre.out")"
expect "smith-preprocessor: #if, #else and #endif say if constexpr and target" "3" "$(grep -c 'if constexpr (target::os == target::macos)' "$work/pre.out")"
expect "smith-preprocessor: c::assert says contract::assert" "2" "$(grep -c 'write `contract::assert(condition)`' "$work/pre.out")"
expect "smith-preprocessor: c::isfinite says math::is_finite" "1" "$(grep -c 'write `math::is_finite(x)`' "$work/pre.out")"
expect "smith-preprocessor: c::SIZE_MAX says math::max(usize)" "1" "$(grep -c 'write `math::max(usize)`' "$work/pre.out")"
run pre/optout.smith >"$work/pre-optout.out"
expect "smith-preprocessor: NOLINT(smith-preprocessor) opts a line out" "0" "$(grep -c 'smith-preprocessor' "$work/pre-optout.out")"
smith lint --quiet pre/plain.c -- -std=gnu2y >"$work/pre-plain.out" 2>&1
expect "smith-preprocessor: a .c file is left alone" "0" "$(grep -c 'smith-preprocessor' "$work/pre-plain.out")"
run pre/good.smith >"$work/pre-good.out"
expect "smith-preprocessor: a module with contract, math, target and constexpr is clean" "0" "$(grep -c 'smith-preprocessor' "$work/pre-good.out")"

# --- the idiom checks (issue #47): sized types, libraries as modules, bounded loops, C23 spellings, constexpr ----------------
# Each check alone (--checks), a failing file and a passing one, and the message that names the fix.
mkdir -p "$work/p/idiom"
idiom() { # check, files...
    check=$1
    shift
    smith lint --quiet --checks="-*,$check" "$@" -- -std=gnu2y $flags -I"$here/include" 2>&1
}
idiom_findings() { # a finding as "file:line:col: message", without the check's name
    findings | sed -e 's/ \[smith-[a-z0-9-]*,-warnings-as-errors\]$//'
}

cat >"$work/p/idiom/sized.smith" <<'EOF2'
module idiom::sized;

import <stddef.h>;
import <stdint.h>;

export c::uint64_t wide(c::size_t n, unsigned x, unsigned long long y, double d, float f) {
    unsigned char byte = 1;
    long count = 2;
    int plain = 3;
    short half = 4;
    return n + x + y + (c::uint64_t)d + (c::uint64_t)f + byte + count + plain + half;
}

export char first(const char *text) {
    return text[0];
}

int main(int argc, char **argv) {
    return argc + (argv == nullptr);
}
EOF2
cat >"$work/p/idiom/sized_ok.smith" <<'EOF2'
module idiom::sized_ok;

export u64 wide(usize n, u32 x, u64 y, f64 d, f32 f) {
    u8 byte = 1;
    i64 count = 2;
    i32 plain = 3;
    i16 half = 4;
    char letter = 'a';
    return n + x + y + (u64)d + (u64)f + byte + (u64)count + (u64)plain + (u64)half + (u64)letter;
}

i32 main(i32 argc, char **argv) {
    return argc + (argv == nullptr);
}
EOF2
idiom smith-sized-types idiom/sized.smith >"$work/sized.out"
sized_short() { idiom_findings | sed -e 's/ (u8 u16 u32 u64, i8 i16 i32 i64, usize, isize, f32, f64)$//'; }
expected="idiom/sized.smith:6:11: error: \`uint64_t\` is a C type, and a .smith file has the sized types: write \`u64\`
idiom/sized.smith:6:28: error: \`size_t\` is a C type, and a .smith file has the sized types: write \`usize\`
idiom/sized.smith:6:38: error: \`unsigned\` is a C type, and a .smith file has the sized types: write \`u32\`
idiom/sized.smith:6:50: error: \`unsigned long long\` is a C type, and a .smith file has the sized types: write \`u64\`
idiom/sized.smith:6:72: error: \`double\` is a C type, and a .smith file has the sized types: write \`f64\`
idiom/sized.smith:6:82: error: \`float\` is a C type, and a .smith file has the sized types: write \`f32\`
idiom/sized.smith:7:5: error: \`unsigned char\` is a C type, and a .smith file has the sized types: write \`u8\`
idiom/sized.smith:8:5: error: \`long\` is a C type, and a .smith file has the sized types: write \`i64\`
idiom/sized.smith:9:5: error: \`int\` is a C type, and a .smith file has the sized types: write \`i32\`
idiom/sized.smith:10:5: error: \`short\` is a C type, and a .smith file has the sized types: write \`i16\`
idiom/sized.smith:11:28: error: \`uint64_t\` is a C type, and a .smith file has the sized types: write \`u64\`
idiom/sized.smith:11:45: error: \`uint64_t\` is a C type, and a .smith file has the sized types: write \`u64\`
idiom/sized.smith:18:1: error: \`int\` is a C type, and a .smith file has the sized types: write \`i32\`
idiom/sized.smith:18:10: error: \`int\` is a C type, and a .smith file has the sized types: write \`i32\`"
expect "smith-sized-types: every C type of a .smith file, at its place, with the sized type to write" "$expected" "$(sized_short <"$work/sized.out")"
expect "smith-sized-types: char stays, and the C spelling of main is reported like any int" "2" "$(grep -c 'sized.smith:\(1[3-9]\|2[0-9]\)' "$work/sized.out")"
expect "smith-sized-types: the message lists the sized types" "14" "$(grep -c '(u8 u16 u32 u64, i8 i16 i32 i64, usize, isize, f32, f64)' "$work/sized.out")"
idiom smith-sized-types idiom/sized_ok.smith >"$work/sized-ok.out"
expect "smith-sized-types: sized types, char and i32 main(i32 argc, char **argv) are clean" "" "$(idiom_findings <"$work/sized-ok.out")"
printf 'unsigned f(unsigned long x) { return (unsigned)x; }\n' >idiom/sized_plain.c
idiom smith-sized-types idiom/sized_plain.c >"$work/sized-plain.out"
expect "smith-sized-types: a .c file is left alone" "" "$(idiom_findings <"$work/sized-plain.out")"

cat >"$work/p/idiom/library.smith" <<'EOF2'
module idiom::library;

import <smith/arena.h>;

export void use(c::SmithArena *arena) {
    void *memory = c::smith_arena_push(arena, 8, 8);
    (void)memory;
}
EOF2
cat >"$work/p/idiom/library_ok.smith" <<'EOF2'
module idiom::library_ok;

import smith::arena;

export void use(arena::Arena *arena) {
    u64 *value = arena::push(arena, u64);
    (void)value;
}
EOF2
cat >"$work/p/idiom/library_c.smith" <<'EOF2'
module idiom::library_c;

import <string.h>;

export void zero(void *memory, usize n) {
    c::memset(memory, 0, n);
}
EOF2
idiom smith-module-library idiom/library.smith >"$work/library.out"
expected="idiom/library.smith:3:1: error: \`import <smith/arena.h>;\` reads smith::arena, a module, through its C header: write \`import smith::arena;\` and call it as \`arena::name(...)\` (a typed push is \`arena::push(&a, T)\`)
idiom/library.smith:5:20: error: \`c::SmithArena\` is \`arena::Arena\` of the module smith::arena, reached through its C header: write \`import smith::arena;\` and \`arena::Arena\` (a typed push is \`arena::push(&a, T)\`)
idiom/library.smith:6:23: error: \`c::smith_arena_push\` is \`arena::push_raw\` of the module smith::arena, reached through its C header: write \`import smith::arena;\` and \`arena::push_raw\` (a typed push is \`arena::push(&a, T)\`)"
expect "smith-module-library: the import of a module's header and its c:: names, with the module's call" "$expected" "$(idiom_findings <"$work/library.out")"
cat >"$work/p/idiom/library_test.smith" <<'EOF2'
module idiom::library_test;

import <smith/test.h>;

export void use(i32 a, i32 b) {
    c::smith_check_equal_i32(a, b);
    c::smith_check(a == b);
}
EOF2
idiom smith-module-library idiom/library_test.smith >"$work/library-test.out"
expected="idiom/library_test.smith:3:1: error: \`import <smith/test.h>;\` reads smith::test, a module, through its C header: write \`import smith::test;\` and call it as \`test::name(...)\`
idiom/library_test.smith:6:8: error: \`c::smith_check_equal_i32\` is \`test::check_equal_i32\` of the module smith::test, reached through its C header: write \`import smith::test;\` and \`test::check_equal_i32\` (or, better, the generic \`test::check_equal(a, b)\`, which takes the type from its first argument)
idiom/library_test.smith:7:8: error: \`c::smith_check\` is \`test::check\` of the module smith::test, reached through its C header: write \`import smith::test;\` and \`test::check\`"
expect "smith-module-library: a named instance of a generic points to the generic" "$expected" "$(idiom_findings <"$work/library-test.out")"
cat >"$work/p/idiom/shape.h" <<'EOF2'
#pragma once
typedef struct {
    int width;
    int height;
} Shape;
EOF2
cat >"$work/p/idiom/designated.smith" <<'EOF2'
module idiom::designated;

import "shape.h";

typedef struct {
    i32 x;
    i32 y;
} Mine;

export void use(void) {
    c::Shape by_position = {3, 4};
    c::Shape by_name = {.width = 3, .height = 4};
    c::Shape one = {3};
    c::Shape none = {};
    Mine own = {1, 2};
    c::Shape *made = &(c::Shape){5, 6};
    (void)by_position;
    (void)by_name;
    (void)one;
    (void)none;
    (void)own;
    (void)made;
}
EOF2
idiom smith-designated-init idiom/designated.smith >"$work/designated.out"
expected="idiom/designated.smith:11:28: error: this initializes \`Shape\`, which another file defines, by position: name the fields (\`{.field = value, ...}\`), so that a field added to it cannot shift the values
idiom/designated.smith:16:33: error: this initializes \`Shape\`, which another file defines, by position: name the fields (\`{.field = value, ...}\`), so that a field added to it cannot shift the values"
expect "smith-designated-init: a positional initializer of another file's struct, and nothing else" "$expected" "$(idiom_findings <"$work/designated.out")"
idiom smith-module-library idiom/library_ok.smith idiom/library_c.smith >"$work/library-ok.out"
expect "smith-module-library: import smith::arena is clean, and so is a C header that is no module" "" "$(idiom_findings <"$work/library-ok.out")"

cat >"$work/p/idiom/loops.smith" <<'EOF2'
module idiom::loops;

export u32 forever(u32 n) {
    for (;;) {
        n++;
        if (n > 8) break;
    }
    while (true) {
        n++;
        if (n > 9) break;
    }
    do {
        n++;
    } while (1);
    return n;
}

export u32 bounded(u32 n) {
    for (u32 i = 0; i < 4; i++) n += i;
    while (n < 10) n++;
    do {
        n++;
    } while (n < 20);
    return n;
}

int main(void) {
    for (;;) {
    }
    return 0;
}
EOF2
cat >"$work/p/idiom/loops.c" <<'EOF2'
int forever(int n) {
    for (;;) {
        if (++n > 8) return n;
    }
}

int main(void) {
    while (1) {
    }
}
EOF2
idiom smith-bounded-loop idiom/loops.smith idiom/loops.c >"$work/loops.out"
expected="idiom/loops.c:2:5: error: \`for (;;)\` never ends on its own: bound the loop by a count (\`for (usize i = 0; i < limit; i++)\`); only \`main\` runs forever
idiom/loops.smith:4:5: error: \`for (;;)\` never ends on its own: bound the loop by a count (\`for (usize i = 0; i < limit; i++)\`); only \`main\` runs forever
idiom/loops.smith:8:5: error: a \`while\` with a constant condition never ends on its own: bound the loop by a count (\`for (usize i = 0; i < limit; i++)\`); only \`main\` runs forever
idiom/loops.smith:12:5: error: a \`do ... while\` with a constant condition never ends on its own: bound the loop by a count (\`for (usize i = 0; i < limit; i++)\`); only \`main\` runs forever"
expect "smith-bounded-loop: for (;;), while (true) and do while (1) outside main, in C and .smith, and what to write" "$expected" "$(idiom_findings <"$work/loops.out")"

cat >"$work/p/idiom/c23.smith" <<'EOF2'
module idiom::c23;

import <stddef.h>;
import <stdbool.h>;

typedef struct {
    u32 a;
    u32 b;
} Pair;

[[noreturn]] void stop();
__attribute__((noreturn)) void halt();

export u32 f(const u8 *p) {
    _Bool flag = p == c::NULL;
    Pair pair = {0};
    u8 bytes[4] = {0};
    return pair.a + bytes[0] + flag;
}
EOF2
cat >"$work/p/idiom/c23_ok.smith" <<'EOF2'
module idiom::c23_ok;

typedef struct {
    u32 a;
    u32 b;
} Pair;

[[noreturn]] void stop();
[[nodiscard]] u32 compute();

export u32 f(const u8 *p) {
    bool flag = p == nullptr;
    Pair pair = {};
    Pair one = {1};
    u8 bytes[4] = {};
    return pair.a + one.a + bytes[0] + flag;
}
EOF2
cat >"$work/p/idiom/c23.c" <<'EOF2'
#include <stdbool.h>
#include <stddef.h>

__attribute__((noreturn)) void halt();
__attribute__((unused, warn_unused_result)) static int compute(void) { return 0; }
_Noreturn void quit(void);

int f(const char *p) {
    int values[3] = {0};
    return (p == NULL) + values[0];
}

_Static_assert(sizeof(int) == 4, "int");
_Alignas(16) static char buffer[16];
static const unsigned long alignment = _Alignof(int);
EOF2
idiom smith-c23 idiom/c23.smith >"$work/c23.out"
expected="idiom/c23.smith:4:9: error: \`<stdbool.h>\` is not needed: \`bool\`, \`true\` and \`false\` are keywords in C23
idiom/c23.smith:12:16: error: \`__attribute__((noreturn))\` has a standard spelling: write \`[[noreturn]]\`
idiom/c23.smith:15:5: error: \`_Bool\` is C99's spelling: write \`bool\`, a keyword in C23
idiom/c23.smith:15:26: error: \`NULL\` is a macro: write \`nullptr\`
idiom/c23.smith:16:17: error: \`{0}\` is C's way to zero a whole object: write \`{}\`
idiom/c23.smith:17:19: error: \`{0}\` is C's way to zero a whole object: write \`{}\`"
expect "smith-c23: NULL, _Bool, <stdbool.h>, {0} and __attribute__((noreturn)) in a .smith file, with the C23 spelling" "$expected" "$(idiom_findings <"$work/c23.out")"
idiom smith-c23 idiom/c23_ok.smith >"$work/c23-ok.out"
expect "smith-c23: nullptr, bool, {}, {1} and [[noreturn]] are clean" "" "$(idiom_findings <"$work/c23-ok.out")"
idiom smith-c23 idiom/c23.c >"$work/c23-c.out"
expected="idiom/c23.c:1:11: error: \`<stdbool.h>\` is not needed: \`bool\`, \`true\` and \`false\` are keywords in C23
idiom/c23.c:4:16: error: \`__attribute__((noreturn))\` has a standard spelling: write \`[[noreturn]]\`
idiom/c23.c:5:16: error: \`__attribute__((unused))\` has a standard spelling: write \`[[maybe_unused]]\`
idiom/c23.c:5:24: error: \`__attribute__((warn_unused_result))\` has a standard spelling: write \`[[nodiscard]]\`
idiom/c23.c:6:1: error: \`_Noreturn\` is C11's spelling: write \`[[noreturn]]\`
idiom/c23.c:9:21: error: \`{0}\` is C's way to zero a whole object: write \`{}\`
idiom/c23.c:10:18: error: \`NULL\` is a macro: write \`nullptr\`
idiom/c23.c:13:1: error: \`_Static_assert\` is C11's spelling: write \`static_assert\`, a keyword in C23
idiom/c23.c:14:1: error: \`_Alignas\` is C11's spelling: write \`alignas\`, a keyword in C23
idiom/c23.c:15:40: error: \`_Alignof\` is C11's spelling: write \`alignof\`, a keyword in C23"
expect "smith-c23: the same in a C file" "$expected" "$(idiom_findings <"$work/c23-c.out")"

# `(void)` as a parameter list is C's habit: in C23 `()` is no parameters. A finding with a fix in a module, none in a .c
# file or a .smith file with no `module` line, and none on a cast or on `sizeof(void)`.
cat >"$work/p/idiom/void_list.smith" <<'EOF2'
module idiom::void_list;

typedef void (*Callback)(void);
u32 plain(void);

u32 plain(void) {
    u32 x = 1;
    (void)x;
    if (x) (void)plain;
    return (u32)sizeof(void) + x;
}

export i32 main(void) {
    return 0;
}
EOF2
cat >"$work/p/idiom/void_list_ok.smith" <<'EOF2'
module idiom::void_list_ok;

typedef void (*Callback)();

u32 plain() {
    u32 x = 1;
    (void)x;
    return (u32)sizeof(void) + x;
}
EOF2
cat >"$work/p/idiom/void_list_plain.smith" <<'EOF2'
u32 plain(void) {
    return 1;
}
EOF2
cat >"$work/p/idiom/void_list.c" <<'EOF2'
int plain(void) {
    return 1;
}
EOF2
idiom smith-c23 idiom/void_list.smith >"$work/void.out"
expected="idiom/void_list.smith:3:26: error: \`(void)\` is C's way to say no parameters: in C23 \`()\` means that, write \`()\`
idiom/void_list.smith:4:11: error: \`(void)\` is C's way to say no parameters: in C23 \`()\` means that, write \`()\`
idiom/void_list.smith:6:11: error: \`(void)\` is C's way to say no parameters: in C23 \`()\` means that, write \`()\`
idiom/void_list.smith:13:17: error: \`(void)\` is C's way to say no parameters: in C23 \`()\` means that, write \`()\`"
expect "smith-c23: (void) parameter lists in a module, and not a cast or sizeof(void)" "$expected" "$(idiom_findings <"$work/void.out")"
idiom smith-c23 idiom/void_list_ok.smith idiom/void_list_plain.smith idiom/void_list.c >"$work/void-ok.out"
expect "smith-c23: () in a module is clean, and (void) in a .c file or a .smith file with no module line is C's own" "" "$(idiom_findings <"$work/void-ok.out")"
mkdir -p "$work/voidfix/idiom" && cp idiom/void_list.smith "$work/voidfix/idiom/void_list.smith"
(cd "$work/voidfix" && smith lint --fix --checks="-*,smith-c23" idiom/void_list.smith -- -std=gnu2y $flags -I"$here/include" >/dev/null 2>&1)
expect "smith-c23: --fix deletes the void of each parameter list, and leaves the casts" "module idiom::void_list;

typedef void (*Callback)();
u32 plain();

u32 plain() {
    u32 x = 1;
    (void)x;
    if (x) (void)plain;
    return (u32)sizeof(void) + x;
}

export i32 main() {
    return 0;
}" "$(cat "$work/voidfix/idiom/void_list.smith")"

cat >"$work/p/idiom/consts.smith" <<'EOF2'
module idiom::consts;

const u32 limit = 4;
const u8 table[] = {1, 2, 3};
constexpr u32 fine = 5;
const char *const name = "idiom";

export u32 f(u32 n) {
    const u32 local = 2;
    return n + limit + table[0] + fine + local + (name != nullptr);
}
EOF2
cat >"$work/p/idiom/consts.c" <<'EOF2'
static const int limit = 4;
static const unsigned char table[] = {1, 2, 3};
static const double half = 0.5;
static const char *const names[] = {"a", "b"};
const int shared = 7;
static constexpr int fine = 5;

int f(int n) {
    const int local = 2;
    return n + limit + table[0] + (int)half + (names[0] != nullptr) + shared + fine + local;
}
EOF2
idiom smith-constexpr idiom/consts.smith idiom/consts.c >"$work/consts.out"
expected="idiom/consts.c:1:18: error: \`const\` object \`limit\` has a constant initializer: write \`constexpr\`
idiom/consts.c:2:28: error: \`const\` array \`table\` has a constant initializer: write \`constexpr\`
idiom/consts.smith:3:11: error: \`const\` object \`limit\` has a constant initializer: write \`constexpr\`
idiom/consts.smith:4:10: error: \`const\` array \`table\` has a constant initializer: write \`constexpr\`"
expect "smith-constexpr: a file-scope const with a constant initializer, in C and .smith; not a float, a pointer, an external, a local" "$expected" "$(idiom_findings <"$work/consts.out")"

cp "$here/templates/defaults/clang-tidy" .clang-tidy
# smith-module (issue #60): a .smith file with no module line gets the line its path implies; tests/ is a root of its own.
mkdir -p "$work/p/tests" "$work/p/mod"
printf 'u32 helper(u32 n) {\n    return n + 1;\n}\n' >mod/loose.smith
printf '// The tests.\n\nvoid test_x(void) {}\n' >tests/loose_test.smith
printf 'i32 main(i32 argc, char *argv[]) {\n    return argc - 1;\n}\n' >main.smith
printf 'import smith::build;\n\nvoid build(build::Build *b) {\n    build::app(b, &(build::App){});\n}\n' >build.smith
printf '// A comment first, then the line.\nmodule mod::fine;\n\nexport u32 helper(u32 n) {\n    return n + 1;\n}\n' >mod/fine.smith
printf 'unsigned helper(unsigned n) { return n; }\n' >mod/plain.c
idiom smith-module mod/loose.smith tests/loose_test.smith main.smith build.smith mod/fine.smith mod/plain.c >"$work/module.out"
expected="mod/loose.smith:1:1: error: this file has no \`module\` line, and every .smith file is a module: write \`module mod::loose;\` at the top (its path from the project's root, \`tests/\` being a root of its own), and \`export\` what other files use
tests/loose_test.smith:1:1: error: this file has no \`module\` line, and every .smith file is a module: write \`module loose_test;\` at the top (its path from the project's root, \`tests/\` being a root of its own), and \`export\` what other files use
main.smith:1:1: error: this file has no \`module\` line, and every .smith file is a module: write \`module main;\` at the top (its path from the project's root, \`tests/\` being a root of its own), and \`export\` what other files use
build.smith:1:1: error: this file has no \`module\` line, and every .smith file is a module: write \`module build;\` at the top (its path from the project's root, \`tests/\` being a root of its own), and \`export\` what other files use"
expect "smith-module: a file with no module line gets the one its path implies (tests/ is a root; main; build); a module, and C, pass" "$(printf '%s\n' "$expected" | sort)" "$(idiom_findings <"$work/module.out" | sort)"
run mod/loose.smith >"$work/module-default.out"
expect "smith-module: it is on in smith's defaults" "1" "$(findings <"$work/module-default.out" | grep -c 'smith-module,')"

# The idiom checks are in the template's .clang-tidy, so a .smith file gets them by default.
printf 'module idiom::all;\n\nexport usize f(usize n) {\n    return n + 1;\n}\n' >idiom/all.smith
run idiom/all.smith >"$work/all-ok.out"
expect "the idiom checks are on in smith's defaults, and a clean module passes them all" "" "$(findings <"$work/all-ok.out")"
run idiom/sized.smith >"$work/all-sized.out"
expect "... and report in a default run" "1" "$(findings <"$work/all-sized.out" | head -n 1 | grep -c 'smith-sized-types')"

# Objective-C: a class with ivars, properties and methods has names that are not simple identifiers (selectors); the
# naming check skips them (it once asserted "Name is not a simple identifier", issue #115): no crash, no finding.
cat >objc.m <<'EOF2'
#import <Foundation/Foundation.h>

@interface Thing : NSObject {
    int _count;
    NSString *Mixed_Name;
}
@property(nonatomic, assign) int size_of;
- (void)setCount:(int)count withName:(NSString *)name;
+ (instancetype)Make_Thing;
@end

@implementation Thing
- (void)setCount:(int)count withName:(NSString *)name {
    _count = count;
    Mixed_Name = name;
}
+ (instancetype)Make_Thing {
    return nil;
}
@end
EOF2
smith lint objc.m >"$work/objc.out" 2>&1
objc_status=$?
expect "Objective-C ivars, properties and methods: lint does not crash and reports nothing" "0 0" "$objc_status $(grep -c 'readability-identifier-naming' "$work/objc.out")"

echo "lint-smith: $((total - failures)) of $total passed"
[ $failures = 0 ]

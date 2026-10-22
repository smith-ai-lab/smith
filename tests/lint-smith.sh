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
cp "$here/templates/defaults/clang-tidy" .clang-tidy
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

int main(void) {
    box::Box b = {2, 3};
    int BadLocal = 4;
    return (int)box::area(b) + BadLocal;
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
    int BadLocal = 0;
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

echo "lint-smith: $((total - failures)) of $total passed"
[ $failures = 0 ]

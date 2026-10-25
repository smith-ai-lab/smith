#!/bin/sh
# Effects, inferred (`shim/effects_infer.cpp`; issue #207, skill smith-lang-effects): each tests/effects/*.c and *.smith
# is compiled with `smith cc -c` and what it prints (the status, then each error and note, the path left off) is compared
# with the file beside it, `<file>.expected`: a pure function over libc, the chain from a pure function to printf, a
# ceiling that fits and one that does not, a write to a global or a function-local static, a call through a function
# pointer, a declaration with no body, with a promise and without, in C and in a module; the editor shows the same
# error; and across files, the link of objects with summaries. A smith without clang inside (the stage0), or whose clang lacks the token hook, is skipped.
# Usage: sh tests/effects_infer.sh path/to/smith [--update]
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
update=${2:-}
here=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_DATA_HOME=$work/data
export XDG_CACHE_HOME=$work/cache
mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
export PATH=$work/bin:$PATH

failures=0
total=0

if [ "$(smith cc --smith-which -c a.c | head -n 1 | cut -d ' ' -f 1,2)" != "clang in-process" ]; then
    echo "skipped: this smith has no clang inside (the stage0)"
    exit 0
fi
printf 'int main(void) { return 0; }\n' >"$work/probe.smith"
if smith cc --smith-lower "$work/probe.smith" 2>&1 | grep -q "token hook"; then
    echo "skipped: this smith's clang lacks the token hook"
    exit 0
fi

# What a compile of the file prints: the status, then the errors and notes.
shown() {
    (cd "$here/tests/effects" && smith cc -std=gnu2y -I "$here/include" -c "$1" -o /dev/null >"$work/out" 2>&1)
    echo "status $?"
    grep -E '^[^ ]+:[0-9]+:[0-9]+: (error|note|warning):' "$work/out"
}

for file in "$here"/tests/effects/*.c "$here"/tests/effects/*.smith; do
    name=$(basename "$file")
    total=$((total + 1))
    shown "$name" >"$work/actual"
    if [ "$update" = "--update" ]; then
        cp "$work/actual" "$file.expected"
    elif diff "$file.expected" "$work/actual" >"$work/diff" 2>&1; then
        echo "ok    $name"
    else
        failures=$((failures + 1))
        echo "FAIL  $name"
        head -20 "$work/diff"
    fi
done

# The editor: clangd shows the same error, for C and for a module.
for name in effects_chain.c effects_chain.smith; do
    total=$((total + 1))
    if smith lsp --check="$here/tests/effects/$name" 2>&1 | grep -q "smith-effect: \`bad\` is \[\[smith::pure\]\]"; then
        echo "ok    lsp $name"
    else
        failures=$((failures + 1))
        echo "FAIL  lsp $name: smith lsp --check shows no effect error"
    fi
done

# Across files: each compile leaves a summary beside its object (`x.o.fx`), and the link of objects that have one checks
# the calls that cross files (smith-cc runs `--smith-effects`): an unannotated function of another file is inferred,
# a function nobody defines is `c`, in C and in modules (a build).
mkdir "$work/cross"
(
    cd "$work/cross" || exit 1
    cat >b.h <<'EOF2'
int plain_add(int);
int noisy(const char *);
int ghost(void);
EOF2
    cat >b.c <<'EOF2'
#include <stdio.h>
#include "b.h"
int plain_add(int x) { return x + 1; }
int noisy(const char *text) { return printf("%s\n", text); }
EOF2
    cat >a.c <<'EOF2'
#include "b.h"
[[smith::pure]] int fine(int x) { return plain_add(x); }
[[smith::pure]] int bad(const char *text) { return noisy(text); }
[[smith::pure]] int lost(void) { return ghost(); }
int main(void) { return fine(1) + bad("x") + lost(); }
EOF2
    smith cc -c b.c -o b.o && smith cc -c a.c -o a.o
)
total=$((total + 1))
actual=$(cd "$work/cross" && smith cc a.o b.o -o program 2>&1 | grep -E 'smith-effect')
expected="a.c:3:52: error: smith-effect: \`bad\` is [[smith::pure]], but it has the effects console: bad -> noisy (a.c:3) -> printf (b.c:4)
a.c:4:41: error: smith-effect: \`lost\` is [[smith::pure]], but it has the effects c: lost -> ghost (a.c:4)"
if [ "$expected" = "$actual" ]; then
    echo "ok    across files, C: the link names the chain through the other file, and a function no file defines is c"
else
    failures=$((failures + 1))
    echo "FAIL  across files, C"
    printf '%s\n' "$expected" >"$work/expected" && printf '%s\n' "$actual" >"$work/actual" && diff "$work/expected" "$work/actual"
fi
# The same on one command that compiles and links, where clang's objects are temporary files with their summaries beside
# them (the linker the driver starts checks them).
total=$((total + 1))
actual=$(cd "$work/cross" && smith cc a.c b.c -o program_one 2>&1 | grep -E 'smith-effect')
if [ "$expected" = "$actual" ]; then
    echo "ok    across files, C: one command that compiles and links names the same chain"
else
    failures=$((failures + 1))
    echo "FAIL  across files, one command"
    printf '%s\n' "$expected" >"$work/expected" && printf '%s\n' "$actual" >"$work/actual" && diff "$work/expected" "$work/actual"
fi
mkdir -p "$work/mods/fx"
cat >"$work/mods/fx/lib.smith" <<'EOF2'
module fx::lib;
import <stdio.h>;
export i32 plain(i32 x) { return x + 1; }
export i32 noisy(i32 x) { c::printf("%d\n", x); return x; }
EOF2
cat >"$work/mods/main.smith" <<'EOF2'
module main;
import fx::lib as lib;
[[smith::pure]] i32 good(i32 x) { return lib::plain(x); }
[[smith::pure]] i32 bad(i32 x) { return lib::noisy(x); }
i32 main() { return good(1) + bad(1) - 4; }
EOF2
total=$((total + 1))
actual=$(cd "$work/mods" && smith build 2>&1 | grep -E 'smith-effect' | sed 's/^main.smith:4:[0-9]*: //')
expected="error: smith-effect: \`main_bad\` is [[smith::pure]], but it has the effects console: main_bad -> fx::lib::noisy (main.smith:4) -> printf (lib.smith:4)"
if [ "$expected" = "$actual" ]; then
    echo "ok    across files, modules: a build's link names the chain through the other module"
else
    failures=$((failures + 1))
    echo "FAIL  across files, modules"
    printf '%s\n' "$expected" >"$work/expected" && printf '%s\n' "$actual" >"$work/actual" && diff "$work/expected" "$work/actual"
fi

# An incremental build gives the verdict of a clean one: only the callee's file is edited (to something pure and then back),
# and the importer, whose object is up to date, is judged by the summary of what changed.
rebuilt() { # the effect errors of a build
    (cd "$work/mods" && smith build 2>&1 | grep -E 'smith-effect' | sed 's/^main.smith:4:[0-9]*: //')
}
printf 'module fx::lib;\nimport <stdio.h>;\nexport i32 plain(i32 x) { return x + 1; }\nexport i32 noisy(i32 x) { return x; }\n' >"$work/mods/fx/lib.smith"
total=$((total + 1))
actual=$(rebuilt)
if [ -z "$actual" ]; then
    echo "ok    incremental: the callee edited to be pure, the importer is no longer in error"
else
    failures=$((failures + 1))
    echo "FAIL  incremental: the importer is still in error after only the callee became pure: $actual"
fi
printf 'module fx::lib;\nimport <stdio.h>;\nexport i32 plain(i32 x) { return x + 1; }\nexport i32 noisy(i32 x) { c::printf("%%d\\n", x); return x; }\n' >"$work/mods/fx/lib.smith"
total=$((total + 1))
actual=$(rebuilt)
expected="error: smith-effect: \`main_bad\` is [[smith::pure]], but it has the effects console: main_bad -> fx::lib::noisy (main.smith:4) -> printf (lib.smith:4)"
if [ "$expected" = "$actual" ]; then
    echo "ok    incremental: the callee edited back to print, the same error as a clean build"
else
    failures=$((failures + 1))
    echo "FAIL  incremental: the error did not come back after only the callee changed"
    printf '%s\n' "$expected" >"$work/expected" && printf '%s\n' "$actual" >"$work/actual" && diff "$work/expected" "$work/actual"
fi

# The same in a C build (main.c and lib.c, the conventions): only lib.c is edited, to be pure and back, and a summary lost
# (the folder removed) is made again with the same verdict. smith has no object cache (its caches are modules and
# packages), so there is no cached object that could miss its summary: an object whose summary is gone is rebuilt.
mkdir "$work/cb"
printf 'int noisy(int);\n' >"$work/cb/lib.h"
printf '#include <stdio.h>\n#include "lib.h"\nint noisy(int x) { printf("%%d\\n", x); return x; }\n' >"$work/cb/lib.c"
printf '#include "lib.h"\n[[smith::pure]] static int bad(int x) { return noisy(x); }\nint main(void) { return bad(1) - 1; }\n' >"$work/cb/main.c"
cbuild() { # the effect errors of a build of the C project
    (cd "$work/cb" && smith build 2>&1 | grep -E 'smith-effect' | sed 's/^main.c:2:[0-9]*: //')
}
cexpected="error: smith-effect: \`bad\` is [[smith::pure]], but it has the effects console: bad -> noisy (main.c:2) -> printf (lib.c:3)"
total=$((total + 1))
actual=$(cbuild)
if [ "$cexpected" = "$actual" ]; then
    echo "ok    C build: the first build names the chain through lib.c"
else
    failures=$((failures + 1))
    echo "FAIL  C build: $actual"
fi
printf '#include "lib.h"\nint noisy(int x) { return x; }\n' >"$work/cb/lib.c"
total=$((total + 1))
actual=$(cbuild)
if [ -z "$actual" ]; then
    echo "ok    C build, incremental: only lib.c became pure, and main.c is no longer in error"
else
    failures=$((failures + 1))
    echo "FAIL  C build, incremental: still in error after only lib.c became pure: $actual"
fi
printf '#include <stdio.h>\n#include "lib.h"\nint noisy(int x) { printf("%%d\\n", x); return x; }\n' >"$work/cb/lib.c"
total=$((total + 1))
actual=$(cbuild)
if [ "$cexpected" = "$actual" ]; then
    echo "ok    C build, incremental: lib.c edited back, the same error as the first build"
else
    failures=$((failures + 1))
    echo "FAIL  C build, incremental: the error did not come back: $actual"
fi
rm -rf "$work/cb/.smith/effects"
total=$((total + 1))
actual=$(cbuild)
if [ "$cexpected" = "$actual" ] && [ -f "$(ls "$work"/cb/.smith/effects/*main.o.fx 2>/dev/null | head -n 1)" ]; then
    echo "ok    C build: the summaries removed, the next build makes them again with the same verdict"
else
    failures=$((failures + 1))
    echo "FAIL  C build: after removing the summaries: $actual"
fi

# The worst-case stack: `<program>.stack` beside a program linked from objects that have summaries, the frames from the
# backend (the numbers `-fstack-usage` gives), summed along the deepest call, a recursion unbounded, a tail call cycle
# not, a call of code with no source with the slack added.
mkdir "$work/sk"
cat >"$work/sk/a.c" <<'EOF2'
#include <stdio.h>
int g(int *);
static int h(int x) { int a[100]; for (int i = 0; i < 100; i++) a[i] = x + i; return g(a); }
int f(int x) { return h(x) + 1; }
int rec(int n) { return n ? rec(n - 1) + 1 : 0; }
int even(int n);
int odd(int n) { if (n == 0) return 0; [[clang::musttail]] return even(n - 1); }
int even(int n) { if (n == 0) return 1; [[clang::musttail]] return odd(n - 1); }
int main(void) { printf("%d\n", f(1) + even(4)); return rec(3); }
EOF2
echo 'int g(int *p) { return p[3]; }' >"$work/sk/b.c"
(
    cd "$work/sk" || exit 1
    smith cc -c a.c -o a.o && smith cc -c b.c -o b.o && smith cc -fstack-usage -c a.c -o ref_a.o && smith cc -fstack-usage -c b.c -o ref_b.o &&
        smith cc a.o b.o -o p
) >"$work/sk/log" 2>&1
frame() { awk -F'\t' -v n="$2" '{ split($1, p, ":"); if (p[length(p)] == n) print $2 }' "$work/sk/$1.su"; }
want=$(($(frame ref_a f) + $(frame ref_a h) + $(frame ref_b g)))
row() { awk -F'\t' -v n="$1" '$1 == n { print $2 " " $3 " " $4 }' "$work/sk/p.stack"; }
total=$((total + 1))
if [ "$(row f)" = "$want $want -" ]; then
    echo "ok    stack: f -> h -> g is the sum of the three frames the backend gave ($want)"
else
    failures=$((failures + 1))
    echo "FAIL  stack: f is '$(row f)', wanted $want ($(cat "$work/sk/log"))"
fi
total=$((total + 1))
case $(row rec) in
unbounded*) echo "ok    stack: recursion is unbounded" ;;
*) failures=$((failures + 1)); echo "FAIL  stack: rec is '$(row rec)'" ;;
esac
total=$((total + 1))
case $(row even) in
unbounded* | "") failures=$((failures + 1)); echo "FAIL  stack: a cycle of tail calls is '$(row even)'" ;;
*) echo "ok    stack: a cycle of musttail calls is bounded" ;;
esac
total=$((total + 1))
if [ "$(row main)" = "unbounded $(($(frame ref_a main) + $(frame ref_a f) + $(frame ref_a h) + $(frame ref_b g))) printf" ]; then
    echo "ok    stack: main names printf as a call of code with no source"
else
    failures=$((failures + 1))
    echo "FAIL  stack: main is '$(row main)'"
fi
total=$((total + 1))
sed -i.bak 's/^int rec.*$//; s/return rec(3)/return 0/' "$work/sk/a.c" && rm "$work/sk/a.c.bak"
if (cd "$work/sk" && smith cc -c a.c -o a.o && SMITH_STACK_SLACK=1000 smith cc a.o b.o -o p) >"$work/sk/log" 2>&1 &&
    [ "$(row main | cut -d' ' -f1)" = "$(($(row main | cut -d' ' -f2) + 1000))" ]; then
    echo "ok    stack: SMITH_STACK_SLACK sets the bytes added for the call of printf"
else
    failures=$((failures + 1))
    echo "FAIL  stack: slack: '$(row main)' $(cat "$work/sk/log")"
fi
rm -f "$work/sk/p.stack"
total=$((total + 1))
if (cd "$work/sk" && smith cc a.c b.c -o p2) >"$work/sk/log" 2>&1 && [ ! -e "$work/sk/p2.stack" ] && [ ! -e "$work/sk/p2.su" ]; then
    echo "ok    stack: one command that compiles and links writes no .stack"
else
    failures=$((failures + 1))
    echo "FAIL  stack: compile and link in one command: $(cat "$work/sk/log")"
fi

echo "$((total - failures)) of $total passed"
[ "$failures" -eq 0 ]

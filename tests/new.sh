#!/bin/sh
# End-to-end tests for `smith new`, with real ninja, clang and git. Git is isolated from the user's configuration: HOME
# is a temporary folder, and the identity comes from the environment except in the no-identity case.
# Usage: sh tests/new.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
mkdir "$work/bin" "$work/home"
ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH=$work/bin:$PATH
export HOME=$work/home GIT_CONFIG_NOSYSTEM=1
export XDG_CACHE_HOME=$work/xdg-cache # the module and package caches of the tests, never the real ones
export GIT_AUTHOR_NAME=Tester GIT_AUTHOR_EMAIL=tester@example.com
export GIT_COMMITTER_NAME=Tester GIT_COMMITTER_EMAIL=tester@example.com

failures=0
total=0
out=
err=
rc=0

capture() {
    out=$("$@" 2>"$work/err")
    rc=$?
    err=$(cat "$work/err")
}

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

expect_contains() { # name, text, needle
    total=$((total + 1))
    case $2 in
    *"$3"*) echo "ok    $1" ;;
    *)
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected to contain: $3"
        echo "  actual:              $2"
        ;;
    esac
}

lines() { printf '%s\n' "$@"; }

has() { case $1 in *"$2"*) echo yes ;; *) echo no ;; esac; } # text, needle

exists() { if [ -e "$1" ] || [ -L "$1" ]; then echo yes; else echo no; fi; }

tree_sum() { # a checksum of every file and link in a folder, git's own data excluded
    (cd "$1" && find . -path ./.git -prune -o \( -type f -o -type l \) -print | sort | while read -r f; do
        if [ -L "$f" ]; then echo "$f -> $(readlink "$f")"; else echo "$f $(cksum <"$f")"; fi
    done)
}

commits() { git -C "$1" rev-list --count HEAD 2>/dev/null || echo 0; }

# The files of a new project, in git's order. smith builds by convention: no build file, no .clang-format or
# .clang-tidy, and no compile_commands.json at the root.
smith_files=".agents/skills/hello-docs/SKILL.md .claude/settings.json .claude/skills .gitignore AGENTS.md knowledge/index.md knowledge/program.md main.smith primes.smith tests/test_primes.smith"
c_files=".agents/skills/hellc-docs/SKILL.md .claude/settings.json .claude/skills .gitignore AGENTS.md knowledge/index.md knowledge/program.md main.c primes.c primes.h tests/test_primes.c"

# The verbs on a new project, in the current folder: label, extension (smith or c), and the program's name.
verbs_on_new_project() {
    label=$1 ext=$2 prog=$3
    case $ext in smith) main=main.smith ;; *) main=main.c ;; esac
    cp "$main" "$main.orig"
    if [ "$ext" = smith ]; then
        printf 'import <stdio.h>;\n\nint main(int argc, char **argv) {\n    for (i32 i = 1; i < argc; i++)\n        c::puts(argv[i]);\n    return 0;\n}\n' >"$main"
    else
        printf '#include <stdio.h>\n\nint main(int argc, char **argv) {\n    for (int i = 1; i < argc; i++)\n        puts(argv[i]);\n    return 0;\n}\n' >"$main"
    fi
    capture smith run a "b c" 'x;y' '5%' -- -Dkeep
    expect "$label: smith run passes its arguments to the program, -D after --" "$(lines a 'b c' 'x;y' '5%' -Dkeep)" \
        "$(printf '%s\n' "$out" | grep -v '^\[')"
    capture smith run
    expect "$label: smith run with none passes none" "" "$(printf '%s\n' "$out" | grep -v '^\[' | tr -d '\n')"
    cp "$main.orig" "$main"
    capture smith test
    expect "$label: smith test passes out of the box" 0 "$rc"
    expect_contains "$label: ... on the two tests of the template" "$out" "2 passed, 0 failed"
    capture smith test --test no_primes
    expect_contains "$label: smith test --test runs one test" "$out" "1 passed, 0 failed"
    capture smith prove
    expect "$label: smith prove succeeds with nothing to prove" 0 "$rc"
    expect_contains "$label: ... and says so" "$out" "nothing to prove"
    test_file=tests/test_primes.$ext
    sed 's/sum(5), 28/sum(5), 27/' "$test_file" >"$test_file.new" && mv "$test_file.new" "$test_file"
    capture smith test
    expect "$label: a failing test fails smith test" 1 "$rc"
    expect_contains "$label: ... and shows both values" "$out$err" "sum(5) = 28"
    capture smith check
    expect "$label: smith check fails when a test does" yes "$([ "$rc" -ne 0 ] && echo yes || echo no)"
    git checkout -q -- "$test_file"
    capture smith check x
    expect_contains "$label: smith check takes no arguments" "$err" "takes no arguments"
    capture smith build check
    expect_contains "$label: smith build check is refused for the verb" "$err" "check is a verb, not a target of smith build; run \`smith check\`"
    capture smith check
    expect "$label: smith check passes on the new project" 0 "$rc"
    if [ "$rc" -ne 0 ]; then echo "$err" | tail -n 30; fi
    capture smith fmt --dry-run
    expect "$label: smith fmt --dry-run is clean with smith's style" 0 "$rc"
    capture smith lint
    expect "$label: smith lint is clean with smith's checks" 0 "$rc"
    printf 'int   misformatted;\n' >>"$main"
    capture smith check
    expect_contains "$label: smith check fails on a misformatted file" "$out$err" "misformatted"
    expect "$label: ... with a nonzero status" yes "$([ "$rc" -ne 0 ] && echo yes || echo no)"
    capture smith fmt
    expect "$label: smith fmt rewrites the sources" 0 "$rc"
    expect_contains "$label: ... to smith's style" "$(tail -n 1 "$main")" "int misformatted;"
    cp "$main.orig" "$main"
    rm "$main.orig"
    printf 'int   BadName = 0;\n' >>"primes.$ext"
    capture smith lint
    expect "$label: smith lint fails on what clang-tidy finds" 1 "$rc"
    expect_contains "$label: ... naming the check" "$out$err" "readability-identifier-naming"
    git checkout -q -- "primes.$ext"
    capture smith build # the log of the last build is the plain build's again
    if "$smith" lsp --version >/dev/null 2>&1; then
        capture "$smith" lsp --check="$main"
        errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
        expect "$label: clangd reports no errors in $main, from .smith/compile_commands.json" 0 "$errors"
        expect "$label: ... with no compile_commands.json at the root" no "$(exists compile_commands.json)"
    fi
    capture smith build release fast
    expect "$label: smith build release fast exits 0" 0 "$rc"
    for mode in debug release fast; do
        expect "$label: the $mode program exists" yes "$(exists build/$mode/$prog)"
        expect "$label: the $mode program runs and prints its line" "hello from $prog" "$(build/$mode/$prog | tail -n 1)"
    done
    expect "$label: the commands are in .smith/compile_commands.json" yes "$(has "$(cat .smith/compile_commands.json)" "\"$main\"")"
    capture smith build -t commands build/debug/main.o
    expect_contains "$label: debug has the sanitizers" "$out" "-fsanitize=address,undefined"
    expect_contains "$label: ... compiles with -std=gnu2y" "$out" "-std=gnu2y"
    expect "$label: ... and has no -DNDEBUG" no "$(has "$out" "-DNDEBUG")"
    capture smith build -t commands build/release/main.o
    expect_contains "$label: release is -O2" "$out" " -O2 "
    expect "$label: ... with assertions on" no "$(has "$out" "-DNDEBUG")"
    expect "$label: ... and no sanitizers" no "$(has "$out" "-fsanitize")"
    capture smith build -t commands build/fast/main.o
    expect_contains "$label: fast has -DNDEBUG" "$out" "-DNDEBUG"
}

# 1. smith new hello: a .smith project, the default.
cd "$work" || exit 1
capture smith new hello
expect "new hello: exits 0" 0 "$rc"
new_out=$out
for f in main.smith primes.smith tests/test_primes.smith .gitignore .claude/settings.json AGENTS.md knowledge/index.md knowledge/program.md .agents/skills/hello-docs/SKILL.md; do
    expect "new hello: creates $f" yes "$(exists "hello/$f")"
done
for f in build.c build.smith .clang-format .clang-tidy compile_commands.json main.c primes.c primes.h; do
    expect "new hello: writes no $f" no "$(exists "hello/$f")"
done
expect_contains "new hello: main.smith names the program" "$(cat hello/main.smith)" 'c::puts("hello from hello");'
expect "new hello: no placeholder is left" "" "$(grep -rF -e '@NAME@' -e '@KEBAB@' hello --exclude-dir=.git --exclude-dir=build --exclude-dir=.smith)"
expect "new hello: .gitignore is the template" "$(cat "$here/templates/new/gitignore")" "$(cat hello/.gitignore)"
expect "new hello: .claude/settings.json is the template" "$(cat "$here/templates/new/claude-settings.json")" \
    "$(cat hello/.claude/settings.json)"
expect "new hello: .claude/settings.json is exactly the plugin settings" '{
  "enabledPlugins": {
    "smith@smith": true
  },
  "extraKnownMarketplaces": {
    "smith": {
      "source": {
        "source": "github",
        "repo": "adam-r-kowalski/smith"
      }
    }
  }
}' "$(cat hello/.claude/settings.json)"
expect "new hello: .claude/skills links to the skills" "../.agents/skills" "$(readlink hello/.claude/skills)"
expect "new hello: no CLAUDE.md, Claude Code reads AGENTS.md" no "$(exists hello/CLAUDE.md)"
expect_contains "new hello: AGENTS.md says there is no build file" "$(cat hello/AGENTS.md)" "There is no build file and no config file"
expect_contains "new hello: ... and how to take over a default" "$(cat hello/AGENTS.md)" "smith fmt --dump-config > .clang-format"
expect_contains "new hello: the concept describes the .smith program" "$(cat hello/knowledge/program.md)" "primes::sum"
expect_contains "new hello: the summary names the plugin" "$new_out" "/plugin marketplace add adam-r-kowalski/smith"
expect_contains "new hello: the summary names the install" "$new_out" "/plugin install smith@smith"
expect "new hello: no missing-entry report" no "$(has "$new_out" 'lacks "smith@smith"')"
expect "new hello: git is on main" main "$(git -C hello symbolic-ref --short HEAD)"
expect "new hello: one commit" 1 "$(commits hello)"
expect "new hello: the commit message" "chore: create hello with smith new" "$(git -C hello log -1 --format=%s)"
expect "new hello: git tracks exactly the written files" "$smith_files" "$(git -C hello ls-files | tr '\n' ' ' | sed 's/ $//')"
expect "new hello: the tree is clean after the commit" "" "$(git -C hello status --porcelain)"
expect "new hello: the first build made the debug program" yes "$(exists hello/build/debug/hello)"
expect "new hello: the summary names the next step" yes "$(has "$new_out" "smith run")"
cd hello || exit 1
capture smith run
expect "new hello: smith run prints the greeting" "hello from hello" "$(echo "$out" | tail -n 1)"
expect "new hello: ... after the line of the sum" "the first 5 primes add up to 28" "$(echo "$out" | tail -n 2 | head -n 1)"
verbs_on_new_project "new hello" smith hello
cd "$work" || exit 1

# 1a. smith new --language c hellc: a C project, the same by convention.
capture smith new --language c hellc
expect "new --language c: exits 0" 0 "$rc"
c_out=$out
for f in main.c primes.c primes.h tests/test_primes.c .gitignore .claude/settings.json AGENTS.md knowledge/index.md knowledge/program.md .agents/skills/hellc-docs/SKILL.md; do
    expect "new --language c: creates $f" yes "$(exists "hellc/$f")"
done
for f in build.c build.smith .clang-format .clang-tidy compile_commands.json main.smith primes.smith; do
    expect "new --language c: writes no $f" no "$(exists "hellc/$f")"
done
expect_contains "new --language c: main.c names the program" "$(cat hellc/main.c)" 'puts("hello from hellc");'
expect_contains "new --language c: the concept describes the C program" "$(cat hellc/knowledge/program.md)" "primes_sum"
expect "new --language c: git tracks exactly the written files" "$c_files" "$(git -C hellc ls-files | tr '\n' ' ' | sed 's/ $//')"
expect "new --language c: the tree is clean after the commit" "" "$(git -C hellc status --porcelain)"
expect "new --language c: the first build made the debug program" yes "$(exists hellc/build/debug/hellc)"
cd hellc || exit 1
capture smith run
expect "new --language c: smith run prints the greeting" "hello from hellc" "$(echo "$out" | tail -n 1)"
verbs_on_new_project "new --language c" c hellc
cd "$work" || exit 1
mkdir langs
cd langs || exit 1
capture smith new --language=c eq
expect "new --language=c: exits 0" 0 "$rc"
expect "new --language=c: writes the C program" yes "$(exists eq/main.c)"
capture smith new --language smith explicit
expect "new --language smith: writes the .smith program" yes "$(exists explicit/main.smith)"
capture smith new explicit_first --language c
expect "new <name> --language c: the flag may follow the name" yes "$(exists explicit_first/main.c)"
capture smith new --language rust bad
expect "new --language rust: exit status" 2 "$rc"
expect_contains "new --language rust: names both languages" "$err" "--language is smith or c, not"
expect_contains "new --language rust: ... and the value" "$err" "rust"
expect "new --language rust: creates nothing" no "$(exists bad)"
capture smith new --language
expect "new --language with no value: exit status" 2 "$rc"
expect_contains "new --language with no value: says so" "$err" "--language needs a value: smith or c"
capture smith new --help
expect "new --help: exits 0" 0 "$rc"
expect_contains "new --help: shows the flag" "$out" "--language smith|c"
cd "$work" || exit 1

# 1b. assert(false) stops debug and release and not fast, in a scratch copy of each generated project.
for pair in "hello smith c::assert" "hellc c assert"; do
    set -- $pair
    dir=$1 ext=$2 macro=$3
    mkdir "abort_$dir" && (cd "$dir" && tar cf - --exclude .git --exclude build --exclude .smith .) | (cd "abort_$dir" && tar xf -)
    sed "s/$macro(sum == 28);/$macro(false);/" "$dir/main.$ext" >"abort_$dir/main.$ext"
    cd "abort_$dir" || exit 1
    capture sh -c "smith build && smith build release fast"
    expect "assert(false) in $ext: the three build" 0 "$rc"
    capture build/debug/"abort_$dir"
    expect "assert(false) in $ext: debug stops" 134 "$rc"
    capture build/release/"abort_$dir"
    expect "assert(false) in $ext: release stops" 134 "$rc"
    capture build/fast/"abort_$dir"
    expect "assert(false) in $ext: fast does not stop" 0 "$rc"
    cd "$work" || exit 1
done

# 1c. smith's libraries build under the conventions' flags (gnu2y, every warning an error) in all three profiles and run,
# found from the #include alone.
mkdir libs && cp -R hellc/primes.c hellc/primes.h hellc/tests libs/
cat >libs/main.c <<'C'
#include <smith/arena.h>
#include <smith/random.h>

#include <stdio.h>

int main(void) {
    SmithKey key = smith_key_make(42);
    alignas(smith_arena_align_max) static unsigned char memory[1024];
    SmithArena arena = smith_arena_make(memory, sizeof memory);
    SmithCategorical categorical = smith_categorical_make(&arena, (const double[]){1, 1, 2}, 3);
    size_t index = smith_categorical_sample(key, categorical);
    float x = smith_normal_float_sample(smith_key_derive(key, 1), (SmithNormalFloat){.mean = 0, .deviation = 1});
    puts(index < 3 && x > -6.0f && x < 6.0f ? "libraries ok" : "libraries wrong");
    return 0;
}
C
cd libs || exit 1
capture sh -c "smith build && smith build release fast"
expect "libraries: the three build under the conventions' flags" 0 "$rc"
if [ "$rc" -ne 0 ]; then echo "$err" | head -n 20; fi
for mode in debug release fast; do
    capture build/$mode/libs
    expect "libraries: the $mode program runs" "libraries ok" "$out"
done
cd "$work" || exit 1

# 1d. The verbs read what the build compiles, in a git copy of the generated C project: prove proves the contracts of the
# sources that are compiled, fmt formats what git sees (and leaves out .gitignore and .clang-format-ignore), lint reports
# what clang-tidy finds, and check reports every part that fails.
mkdir verbs
(cd hellc && tar cf - --exclude .git --exclude build --exclude .smith .) | (cd verbs && tar xf -)
cd verbs || exit 1
git init -q -b main && git add -A && git commit -q -m first
printf '#pragma once\n#include <stdint.h>\n\nint32_t clamp(int32_t x, int32_t low, int32_t high);\nint32_t draw_health(int32_t health, int32_t max_health);\nint32_t pinned(int32_t x);\n' >clamp.h
{ printf '#include "clamp.h"\n'; cat "$here/tests/prove/clamp.c"; } >clamp.c
sed 's|#include "primes.h"|#include "clamp.h"\n#include "primes.h"|' main.c >main.c.new && mv main.c.new main.c # clamp.c is found by the include
capture smith prove
if [ "$(has "$err" "has no prover")" = yes ]; then
    echo "skip  new verbs: smith prove (this smith has no prover)"
else
    expect "new verbs: smith prove proves the contracts of the compiled sources" 0 "$rc"
    expect_contains "new verbs: ... both functions" "$out" "2 proved, 0 failed"
    printf '#include <smith/contract.h>\n\n#include "clamp.h"\n\nint32_t pinned(int32_t x) {\n    smith_requires(x >= 0);\n    return clamp(x, 10, 5);\n}\n' >bad_caller.c
    printf '#pragma once\n#include "clamp.h"\n' >bad_caller.h
    sed 's|#include "clamp.h"|#include "bad_caller.h"\n#include "clamp.h"|' main.c >main.c.new && mv main.c.new main.c
    capture smith prove
    expect "new verbs: a caller that breaks a contract fails smith prove" 1 "$rc"
    expect_contains "new verbs: ... at the call" "$out" "bad_caller.c:7: the call of clamp may break smith_requires(low <= high)"
    rm bad_caller.c bad_caller.h
    sed '/#include "bad_caller.h"/d' main.c >main.c.new && mv main.c.new main.c
fi
capture smith fmt
expect "new verbs: smith fmt formats the files of a tree" 0 "$rc"
printf 'int   untracked_file;\n' >extra.c
printf 'int   ignored_file;\n' >ignored.c
printf 'int   left_alone;\n' >left.c
printf 'ignored.c\n' >>.gitignore
printf 'left.c\n' >.clang-format-ignore
capture smith fmt
expect "new verbs: smith fmt formats a file git has not seen yet" "int untracked_file;" "$(cat extra.c)"
expect "new verbs: ... not one .gitignore leaves out" "int   ignored_file;" "$(cat ignored.c)"
expect "new verbs: ... nor one .clang-format-ignore names" "int   left_alone;" "$(cat left.c)"
rm extra.c ignored.c left.c .clang-format-ignore
# Options alone are the project's verb (with standard input closed a clang-format waiting for it would fail, and with
# a terminal it would hang); a .metal file is formatted too, as C++.
printf 'kernel   void  f( device float*a [[buffer(0)]] ){ a[0]=1;}\n' >shader.metal
capture smith fmt --dry-run </dev/null
expect "new verbs: smith fmt --dry-run is the project's check, and a .metal file fails it" 1 "$rc"
expect_contains "new verbs: ... naming the .metal file" "$out$err" "shader.metal"
capture smith fmt </dev/null
expect "new verbs: smith fmt formats a .metal file" "kernel void f(device float *a [[buffer(0)]]) {" "$(head -n 1 shader.metal)"
capture smith fmt --dry-run </dev/null
expect "new verbs: ... and then the dry run passes" 0 "$rc"
capture smith lint --quiet </dev/null
expect "new verbs: smith lint --quiet is the project's lint" 0 "$rc"
rm shader.metal
git checkout -q -- .gitignore
printf 'int   BadName = 0;\n' >>primes.c
capture smith lint
expect "new verbs: smith lint fails on what clang-tidy finds" 1 "$rc"
expect_contains "new verbs: ... naming the check" "$out$err" "readability-identifier-naming"
expect_contains "new verbs: ... and how many files" "$err" "clang-tidy failed on 1 of"
capture smith check
expect "new verbs: smith check fails when parts do" 1 "$rc"
expect_contains "new verbs: ... names the parts that failed, in order" "$err" "smith check: failed: fmt lint"
expect_contains "new verbs: ... and the parts that passed" "$err" "smith check: test ok"
git checkout -q -- primes.c
cd "$work" || exit 1

# 1e. Two programs that link one source: each proof of it gets the full flags (the first once cut the compile's flags
# for every later proof of the same source, which then failed to find its header).
mkdir shared
cd shared || exit 1
mkdir inc
printf '#pragma once\nint shared_double(int x);\n' >inc/shared.h
printf '#include <smith/contract.h>\n\n#include "shared.h"\n\nint shared_double(int x) {\n    smith_requires(0 <= x && x <= 1000);\n    int result = 2 * x;\n    smith_ensures(result <= 2000);\n    return result;\n}\n' >shared.c
printf '#include "shared.h"\n\nint main(void) {\n    return shared_double(1) - 2;\n}\n' >a.c
printf '#include "shared.h"\n\nint main(void) {\n    return shared_double(2) - 4;\n}\n' >b.c
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const flags[] = {"-std=gnu2y", "-I", "inc", "-I", ".smith/include", nullptr};
    const char *const a_flags[] = {"-std=gnu2y", "-I", "inc", "-I", ".smith/include", "-DA", nullptr};
    const char *const b_flags[] = {"-std=gnu2y", "-I", "inc", "-I", ".smith/include", "-DB", nullptr};
    SmithObject shared = {.compiler = "smith-cc", .source = "shared.c", .output = "build/shared.o", .flags = flags};
    SmithObject a = {.compiler = "smith-cc", .source = "a.c", .output = "build/a.o", .flags = a_flags};
    SmithObject b_object = {.compiler = "smith-cc", .source = "b.c", .output = "build/b.o", .flags = b_flags};
    smith_object(b, &shared);
    smith_object(b, &a);
    smith_object(b, &b_object);
    SmithProgram first = {.linker = "smith-cc", .output = "build/first",
                          .objects = (const SmithObject *[]){&shared, &a, nullptr}};
    SmithProgram second = {.linker = "smith-cc", .output = "build/second",
                           .objects = (const SmithObject *[]){&shared, &b_object, nullptr}};
    smith_program(b, &first);
    smith_program(b, &second);
    smith_default(b, smith_cat(b, first.step.outputs, second.step.outputs, nullptr));
}
C
capture smith prove
if [ "$(has "$err" "has no prover")" = yes ]; then
    echo "skip  new shared: smith prove (this smith has no prover)"
else
    expect "new shared: smith prove proves a source two programs link, both times" 0 "$rc"
    expect_contains "new shared: ... the contract of shared.c is proved" "$out" "1 proved, 0 failed"
    expect "new shared: ... and the header is found by both proofs" no "$(has "$out$err" "file not found")"
fi
cd "$work" || exit 1

# 2. A second run creates nothing.
before=$(tree_sum hello)
capture smith new hello
expect "second run: exits 0" 0 "$rc"
expect "second run: changes no bytes" "$before" "$(tree_sum hello)"
expect "second run: no new commit" 1 "$(commits hello)"
expect_contains "second run: everything is kept" "$out" "0 created, 10 kept"

# 3. No argument: the folder's name, and its own main.c kept, with no program of the template beside it.
mkdir demo && printf 'int main(void) { return 0; }\n' >demo/main.c
cd demo || exit 1
capture smith new
expect "no argument: exits 0" 0 "$rc"
expect "no argument: keeps main.c" "int main(void) { return 0; }" "$(cat main.c)"
expect "no argument: names the program demo" yes "$(exists build/debug/demo)"
expect "no argument: leaves out cd" no "$(has "$out" "cd demo")"
expect "no argument: no concept about a main.c it did not write" no "$(exists knowledge/program.md)"
expect "no argument: the index lists no such concept" no "$(has "$(cat knowledge/index.md)" "program.md")"
capture smith docs .
expect "no argument: the docs pass" 0 "$rc"
expect "no argument: writes no program beside the project's own" "" "$(ls primes.* main.smith tests 2>/dev/null)"
cd "$work" || exit 1
mkdir own_smith
printf 'int main(void) {\n    return 0;\n}\n' >own_smith/main.smith
capture smith new --language c own_smith
expect "an own main.smith: exits 0" 0 "$rc"
expect "an own main.smith: --language c writes no C program beside it" "" "$(ls own_smith/main.c own_smith/primes.* 2>/dev/null)"

# 4. Inside an existing repository.
mkdir outer && git -C outer init -q -b main && git -C outer commit -q --allow-empty -m first
cd outer || exit 1
capture smith new inner
cd "$work" || exit 1
expect "inside a repository: exits 0" 0 "$rc"
expect "inside a repository: no new repository" no "$(exists outer/inner/.git)"
expect "inside a repository: no commit" 1 "$(commits outer)"
expect "inside a repository: nothing staged" "" "$(git -C outer diff --cached --name-only)"

# 5. No git identity.
mkdir noid
(
    unset GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL EMAIL
    export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=user.useConfigOnly GIT_CONFIG_VALUE_0=true
    cd noid && smith new proj >"$work/noid.out" 2>"$work/noid.err"
    echo $? >"$work/noid.rc"
)
expect "no identity: exits 0" 0 "$(cat "$work/noid.rc")"
expect "no identity: files written" yes "$(exists noid/proj/main.smith)"
expect "no identity: files staged" yes "$(git -C noid/proj diff --cached --name-only | grep -q '^main.smith$' && echo yes || echo no)"
expect "no identity: no commit" 0 "$(commits noid/proj)"
expect_contains "no identity: the message says the commit was skipped" "$(cat "$work/noid.out")" "commit skipped"

# 6. Errors.
capture smith new -bad
expect "bad name: exit status" 2 "$rc"
expect "bad name: message" "smith: -bad cannot name a program; use letters, digits, _, . and -" "$err"
capture smith new 'a b'
expect "bad name with a space: exit status" 2 "$rc"
mkdir 'bad name'
cd 'bad name' || exit 1
capture smith new
expect "bad folder name: exit status" 2 "$rc"
expect_contains "bad folder name: names the parent folder way" "$err" "smith new <name> from the parent folder picks another name"
expect "bad folder name: writes nothing" no "$(exists main.smith)"
cd "$work" || exit 1
capture smith new one two
expect "two arguments: exit status" 2 "$rc"
expect_contains "two arguments: usage" "$err" "at most one name"
expect "two arguments: creates nothing" no "$(exists one)"

# 9. An existing build.c is kept and gets no program; the first build is the project's own.
mkdir has_build
cat >has_build/build.c <<'BUILD'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep stamp = {
        .command = (const char *[]){"touch", "$out", nullptr},
        .outputs = (const char *[]){"own.stamp", nullptr},
    };
    smith_step(b, &stamp);
    smith_default(b, stamp.outputs);
}
BUILD
before=$(cat has_build/build.c)
capture smith new has_build
expect "existing build.c: exits 0" 0 "$rc"
expect "existing build.c: kept" "$before" "$(cat has_build/build.c)"
expect "existing build.c: no main.smith" no "$(exists has_build/main.smith)"
expect "existing build.c: no config is written" no "$(exists has_build/.clang-tidy)"
expect "existing build.c: the project's own build ran" yes "$(exists has_build/own.stamp)"

# 10. Another build system: no program, no build file and no first build.
mkdir has_make
printf 'all:\n\t@echo made\n' >has_make/Makefile
printf 'int main(void) { return 0; }\n' >has_make/other.c
capture smith new has_make
expect "Makefile: exits 0" 0 "$rc"
expect "Makefile: writes no build.c" no "$(exists has_make/build.c)"
expect "Makefile: no main.smith" no "$(exists has_make/main.smith)"
expect "Makefile: no first build" no "$(exists has_make/.smith)"
expect "Makefile: no concept about the program" no "$(exists has_make/knowledge/program.md)"
expect "Makefile: the index lists nothing but is there" "okf_version" \
    "$(sed -n 's/^\(okf_version\):.*/\1/p' has_make/knowledge/index.md)"
expect "Makefile: the index ends with its text, not a dangling item" yes "$(has "$(tail -n 1 has_make/knowledge/index.md)" "Read only the concepts")"
expect "Makefile: writes no rules" no "$(exists has_make/.clang-format)"
expect "Makefile: writes the plugin settings" yes "$(exists has_make/.claude/settings.json)"
expect_contains "Makefile: the message names the Makefile" "$out" "This project builds with a Makefile, so smith new wrote no program"
expect_contains "Makefile: the message says to port it" "$out" "write a build.c that does what the Makefile does"
expect_contains "Makefile: the message names the skill" "$out" "smith-build skill"
expect "Makefile: commits only the files written" \
    ".agents/skills/has-make-docs/SKILL.md .claude/settings.json .claude/skills .gitignore AGENTS.md knowledge/index.md" \
    "$(git -C has_make ls-files | tr '\n' ' ' | sed 's/ $//')"
capture smith docs has_make
expect "Makefile: the docs pass" 0 "$rc"
mkdir has_cmake
: >has_cmake/CMakeLists.txt
capture smith new has_cmake
expect_contains "CMakeLists.txt: the message names it" "$out" "This project builds with a CMakeLists.txt, so smith new wrote no program"
expect "CMakeLists.txt: no main.smith" no "$(exists has_cmake/main.smith)"

# 11. An existing .gitignore is kept; the lines it lacks are reported.
mkdir has_ignore
printf '/build/  \nnode_modules/\n' >has_ignore/.gitignore
capture smith new has_ignore
expect "gitignore: kept untouched" "$(printf '/build/  \nnode_modules/')" "$(cat has_ignore/.gitignore)"
expect_contains "gitignore: reports /.cache/" "$out" "lacks /.cache/"
expect "gitignore: does not report /build/ (trailing spaces ignored)" no \
    "$(has "$out" "lacks /build/")"

# 11b. An existing .claude/settings.json is kept; the missing plugin entry is reported.
mkdir -p has_settings/.claude
printf '{"permissions": {}}\n' >has_settings/.claude/settings.json
capture smith new has_settings
expect "settings: kept untouched" '{"permissions": {}}' "$(cat has_settings/.claude/settings.json)"
expect_contains "settings: reports the missing entry" "$out" 'lacks "smith@smith"'
expect_contains "settings: says what to add" "$out" '"smith@smith": true under enabledPlugins'
mkdir -p has_settings2/.claude
printf '{"enabledPlugins": {"smith@smith": false}}\n' >has_settings2/.claude/settings.json
capture smith new has_settings2
expect "settings with the entry: no report" no "$(has "$out" 'lacks "smith@smith"')"

# 13. Docs. The new project has knowledge, a skill and AGENTS.md, and smith docs and smith check hold them.
cd "$work/hello" || exit 1
expect "docs: the skill is named for the program" "name: hello-docs" "$(grep '^name:' .agents/skills/hello-docs/SKILL.md)"
expect_contains "docs: AGENTS.md links the skill" "$(cat AGENTS.md)" "(.agents/skills/hello-docs/SKILL.md)"
expect_contains "docs: AGENTS.md links the knowledge" "$(cat AGENTS.md)" "(knowledge/index.md)"
expect "docs: the index lists the concept with its own description" \
    "- [The program](program.md): $(sed -n 's/^description: //p' knowledge/program.md)" "$(grep '^- \[' knowledge/index.md)"
capture smith docs
expect "docs: smith docs passes on a new project" 0 "$rc"
expect_contains "docs: ... on the concept of the template" "$out" "smith docs: 1 concepts, 0 problems"
expect_contains "docs: the allowed types are declared in the index" "$(cat knowledge/index.md)" \
    "types: convention, architecture, contract, design"
sed 's/^types: .*/types: contract/' knowledge/index.md >index.new && mv index.new knowledge/index.md
capture smith docs
expect_contains "docs: a type the index no longer declares is refused" "$err" "type 'architecture' is not one of contract"
git checkout -q -- knowledge/index.md
capture smith docs .
expect "docs: smith docs . checks with the default types" 0 "$rc"
capture smith build docs
expect "docs: smith build docs is refused, the verb runs it" 2 "$rc"

# smith check holds the docs: a conflict marker fails it.
printf '=======\n' >>knowledge/program.md
capture smith check
expect "docs: smith check fails on a document with a conflict marker" yes "$([ "$rc" -ne 0 ] && echo yes || echo no)"
expect_contains "docs: ... and says where" "$out$err" "knowledge/program.md:"
git checkout -q -- knowledge/program.md
capture smith check
expect "docs: smith check passes again" 0 "$rc"
cd "$work" || exit 1

# Breaking each rule fails smith docs, in a copy.
broken() { # name, expected message, commands that break the copy in the current folder
    rm -rf "$work/brk" && mkdir "$work/brk"
    (cd hello && tar cf - --exclude .git --exclude build --exclude .smith .) | (cd "$work/brk" && tar xf -)
    (cd "$work/brk" && eval "$3") >/dev/null 2>&1
    capture smith docs "$work/brk"
    expect "docs breaks: $1: exits 1" 1 "$rc"
    expect_contains "docs breaks: $1: says so" "$err" "$2"
}
broken "a type the project does not allow" "knowledge/program.md: type 'poem' is not one of convention, architecture, contract, design" \
    "sed 's/^type: .*/type: poem/' knowledge/program.md >p && mv p knowledge/program.md"
broken "a concept without a title" "knowledge/program.md: frontmatter lacks title" \
    "grep -v '^title:' knowledge/program.md >p && mv p knowledge/program.md"
broken "a concept without a description" "knowledge/program.md: frontmatter lacks description" \
    "grep -v '^description:' knowledge/program.md >p && mv p knowledge/program.md"
broken "a concept without frontmatter" "knowledge/program.md: no frontmatter" "printf '# Program\n' >knowledge/program.md"
broken "a concept that is not kebab-case" "knowledge/Program_Two.md: file name is not kebab-case" \
    "cp knowledge/program.md knowledge/Program_Two.md"
broken "a concept the index does not list" "knowledge/index.md: does not list second.md" \
    "cp knowledge/program.md knowledge/second.md"
broken "an index item that is no concept" "knowledge/index.md: lists ghost.md, which is not a concept on disk" \
    "printf -- '- [Ghost](ghost.md): Nothing.\n' >>knowledge/index.md"
broken "an index description that differs" "knowledge/index.md: description of program.md differs from its own" \
    "sed 's/promise\./promise, more./' knowledge/index.md >p && mv p knowledge/index.md"
broken "an index without okf_version" "knowledge/index.md: frontmatter lacks okf_version" \
    "grep -v '^okf_version' knowledge/index.md >p && mv p knowledge/index.md"
broken "a skill whose name is not its folder" ".agents/skills/hello-docs/SKILL.md: name 'other' does not match its folder" \
    "sed 's/^name: .*/name: other/' .agents/skills/hello-docs/SKILL.md >p && mv p .agents/skills/hello-docs/SKILL.md"
broken "a skill without a description" ".agents/skills/hello-docs/SKILL.md: frontmatter lacks description" \
    "grep -v '^description:' .agents/skills/hello-docs/SKILL.md >p && mv p .agents/skills/hello-docs/SKILL.md"
broken "a broken link" "AGENTS.md: broken link nowhere.md" "printf '[x](nowhere.md)\n' >>AGENTS.md"
broken "a conflict marker" "AGENTS.md:" "printf '<<<<<<< HEAD\n' >>AGENTS.md"
broken "knowledge linking a skill" "knowledge/program.md: links the skill ../.agents/skills/hello-docs/SKILL.md; knowledge never links skills" \
    "printf '[how](../.agents/skills/hello-docs/SKILL.md)\n' >>knowledge/program.md"
broken "no .claude/skills" ".claude/skills: missing; it must be a symlink to ../.agents/skills" "rm .claude/skills"
broken "a .claude/skills that points elsewhere" ".claude/skills: links to ../skills, not ../.agents/skills" \
    "rm .claude/skills && ln -s ../skills .claude/skills"
broken "a .claude/skills that is a folder" ".claude/skills: is not a symlink" \
    "rm .claude/skills && mkdir .claude/skills"

# A name with more than letters: the skill's name is kebab-case and everything passes.
capture smith new My_Proj.x
expect "docs: a name with _ and . exits 0" 0 "$rc"
expect "docs: ... its skill is my-proj-x-docs" yes "$(exists My_Proj.x/.agents/skills/my-proj-x-docs/SKILL.md)"
expect "docs: ... its skill's name is its folder" "name: my-proj-x-docs" "$(grep '^name:' My_Proj.x/.agents/skills/my-proj-x-docs/SKILL.md)"
expect_contains "docs: ... its concept names the program" "$(cat My_Proj.x/knowledge/program.md)" "hello from My_Proj.x"
(cd My_Proj.x && smith docs) >"$work/docs.out" 2>&1
expect "docs: ... smith docs passes" 0 "$?"

# 12. The template's rules stay smith's: every rule and option of smith's own files is in the template's.
# Allowed differences: smith-only assert macro, typedef names, unity-build include check.
tidy_items() { # file: one line per Checks entry and per CheckOptions key
    awk '
        /^Checks:/ { mode = "checks"; next }
        /^[A-Za-z]+:/ && !/^CheckOptions:/ { mode = ""; next }
        /^CheckOptions:/ { mode = "options"; next }
        mode == "checks" && /^  [^ #]/ { gsub(/[ ,]/, ""); print "check " $0 }
        mode == "options" && /^  [a-zA-Z]/ { sub(/:.*/, ""); gsub(/ /, ""); print "option " $0 }
    ' "$1"
}
missing_rules() { # smith's file, the template: what the template lacks
    tidy_items "$1" | grep -v -e 'bugprone-suspicious-include' -e 'bugprone-assert-side-effect.AssertMacros' \
        -e 'readability-identifier-naming.TypedefIgnoredRegexp' | while read -r kind item; do
        tidy_items "$2" | grep -qxF "$kind $item" || echo "$kind $item"
    done
}
expect "rules: the template keeps every rule of smith's .clang-tidy" "" \
    "$(missing_rules "$here/.clang-tidy" "$here/templates/defaults/clang-tidy")"
grep -v 'readability-function-size.LineThreshold' "$here/templates/defaults/clang-tidy" >"$work/tidy-without"
expect_contains "rules: a missing option is caught" "$(missing_rules "$here/.clang-tidy" "$work/tidy-without")" \
    "readability-function-size.LineThreshold"
grep -v 'misc-no-recursion' "$here/templates/defaults/clang-tidy" >"$work/tidy-without"
expect_contains "rules: a missing check is caught" "$(missing_rules "$here/.clang-tidy" "$work/tidy-without")" \
    "misc-no-recursion"
expect "rules: the template's .clang-format has smith's rules, comments aside" \
    "$(grep -v '^#' "$here/.clang-format")" "$(grep -v '^#' "$here/templates/defaults/clang-format")"

echo
echo "$total new checks, $failures failed"
[ "$failures" -eq 0 ]

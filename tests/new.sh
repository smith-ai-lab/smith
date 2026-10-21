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
ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" # the generated build.c names smith-cc as its compiler
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

# 1. smith new hello.
cd "$work" || exit 1
capture smith new hello
expect "new hello: exits 0" 0 "$rc"
new_out=$out
for f in build.c main.c primes.c primes.h tests/test_primes.c .gitignore .clang-format .clang-tidy .claude/settings.json; do
    expect "new hello: creates $f" yes "$(exists "hello/$f")"
done
expect_contains "new hello: build.c names the program" "$(cat hello/build.c)" "build/%s/hello"
expect "new hello: build.c has no placeholder left" "" "$(grep -F '@NAME@' hello/build.c hello/main.c)"
expect_contains "new hello: main.c names the program" "$(cat hello/main.c)" 'puts("hello from hello");'
expect "new hello: .gitignore is the template" "$(cat "$here/templates/new/gitignore")" "$(cat hello/.gitignore)"
expect "new hello: .clang-format is the template" "$(cat "$here/templates/new/clang-format")" "$(cat hello/.clang-format)"
expect "new hello: .clang-tidy is the template" "$(cat "$here/templates/new/clang-tidy")" "$(cat hello/.clang-tidy)"
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
for f in AGENTS.md knowledge/index.md knowledge/program.md .agents/skills/hello-docs/SKILL.md; do
    expect "new hello: creates $f" yes "$(exists "hello/$f")"
done
expect "new hello: .claude/skills links to the skills" "../.agents/skills" "$(readlink hello/.claude/skills)"
expect "new hello: no CLAUDE.md, Claude Code reads AGENTS.md" no "$(exists hello/CLAUDE.md)"
expect "new hello: AGENTS.md, knowledge and the skill have no placeholder left" "" \
    "$(grep -rF -e '@NAME@' -e '@KEBAB@' hello/AGENTS.md hello/knowledge hello/.agents hello/build.c)"
expect_contains "new hello: the summary names the plugin" "$new_out" "/plugin marketplace add adam-r-kowalski/smith"
expect_contains "new hello: the summary names the install" "$new_out" "/plugin install smith@smith"
expect "new hello: no missing-entry report" no "$(has "$new_out" 'lacks "smith@smith"')"
expect "new hello: git is on main" main "$(git -C hello symbolic-ref --short HEAD)"
expect "new hello: one commit" 1 "$(commits hello)"
expect "new hello: the commit message" "chore: create hello with smith new" "$(git -C hello log -1 --format=%s)"
expect "new hello: git tracks exactly the written files" \
    ".agents/skills/hello-docs/SKILL.md .clang-format .clang-tidy .claude/settings.json .claude/skills .gitignore AGENTS.md build.c knowledge/index.md knowledge/program.md main.c primes.c primes.h tests/test_primes.c" \
    "$(git -C hello ls-files | tr '\n' ' ' | sed 's/ $//')"
expect "new hello: the tree is clean after the commit" "" "$(git -C hello status --porcelain)"
expect "new hello: the first build made the debug program" yes "$(exists hello/build/debug/hello)"
expect "new hello: the summary names the next step" yes "$(has "$new_out" "smith run")"
cd hello || exit 1
capture smith run
expect "new hello: smith run prints the greeting" "hello from hello" "$(echo "$out" | tail -n 1)"
capture smith build release
expect "new hello: smith build release exits 0" 0 "$rc"
capture smith build fast
expect "new hello: smith build fast exits 0" 0 "$rc"
for mode in debug release fast; do
    expect "new hello: the $mode program exists" yes "$(exists build/$mode/hello)"
    expect "new hello: the $mode program runs and prints its line" "hello from hello" "$(build/$mode/hello | tail -n 1)"
done
expect "new hello: build.c turns modules on" yes "$(grep -q 'smith_modules(b' build.c && echo yes || echo no)"
expect "new hello: the builds compile with modules" 3 \
    "$(grep "fmodules-cache-path=$XDG_CACHE_HOME/smith/modules" .smith/build.ninja | grep -c ' -c main.c ')"
expect "new hello: the module cache is the shared one" yes "$(exists "$XDG_CACHE_HOME/smith/modules/modules.timestamp")"
expect "new hello: compile_commands.json has the module flags" yes "$(has "$(cat compile_commands.json)" "fmodule")"
expect "new hello: compile_commands has main.c" yes "$(grep -q '"main.c"' compile_commands.json && echo yes || echo no)"
expect "new hello: compile_commands has build.c" yes "$(grep -q '"build.c"' compile_commands.json && echo yes || echo no)"
debug_command=$(awk '/"file"/ { m = ($0 ~ /"file": "main.c"/); if (m) n++ } m && n == 1' compile_commands.json)
release_command=$(awk '/"file"/ { m = ($0 ~ /"file": "main.c"/); if (m) n++ } m && n == 2' compile_commands.json)
fast_command=$(awk '/"file"/ { m = ($0 ~ /"file": "main.c"/); if (m) n++ } m && n == 3' compile_commands.json)
expect "new hello: the first main.c command is the debug one" yes "$(has "$debug_command" "build/debug/main.o")"
expect "new hello: the debug command has the sanitizers" yes "$(has "$debug_command" "-fsanitize=address,undefined")"
expect "new hello: the second main.c command is the release one" yes "$(has "$release_command" "build/release/main.o")"
expect "new hello: the third main.c command is the fast one" yes "$(has "$fast_command" "build/fast/main.o")"
expect "new hello: debug has no -DNDEBUG" no "$(has "$debug_command" "-DNDEBUG")"
expect "new hello: release has no -DNDEBUG" no "$(has "$release_command" "-DNDEBUG")"
expect "new hello: fast has -DNDEBUG" yes "$(has "$fast_command" "-DNDEBUG")"
expect "new hello: release is -O2" yes "$(has "$release_command" "-O2")"
expect "new hello: fast is -O2" yes "$(has "$fast_command" "-O2")"
expect "new hello: release has no sanitizers" no "$(has "$release_command" "-fsanitize")"
for mode in debug release fast; do
    eval "command=\$${mode}_command"
    expect "new hello: $mode compiles with -std=gnu2y" yes "$(has "$command" "-std=gnu2y")"
    expect "new hello: $mode does not use gnu23" no "$(has "$command" "-std=gnu23")"
done
expect "new hello: the build.c header lists release" yes "$(has "$(head -n 12 build.c)" "smith build release")"
expect "new hello: the build.c header lists fast" yes "$(has "$(head -n 12 build.c)" "smith build fast")"

# The verbs run the tasks of their names. run takes arguments for the program, test finds no tests, check is the checks.
cp main.c main.c.orig
cat >main.c <<'C'
#include <stdio.h>

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++)
        puts(argv[i]);
    return 0;
}
C
capture smith run a "b c" 'x;y' '5%' -- -Dkeep
expect "new hello: smith run passes its arguments to the program, -D after --" "$(lines a 'b c' 'x;y' '5%' -Dkeep)" \
    "$(printf '%s\n' "$out" | grep -v '^\[')"
capture smith run
expect "new hello: smith run with none passes none" "" "$(printf '%s\n' "$out" | grep -v '^\[' | tr -d '\n')"
cp main.c.orig main.c
capture smith test
expect "new hello: smith test passes out of the box" 0 "$rc"
expect_contains "new hello: ... on the two tests of the template" "$out" "2 passed, 0 failed"
capture smith test --test no_primes
expect_contains "new hello: smith test --test runs one test" "$out" "1 passed, 0 failed"
capture smith prove
expect "new hello: smith prove succeeds with nothing to prove" 0 "$rc"
expect_contains "new hello: ... and says so" "$out" "nothing to prove"
sed 's/primes_sum(5), 28/primes_sum(5), 27/' tests/test_primes.c >tests/test_primes.c.new && mv tests/test_primes.c.new tests/test_primes.c
capture smith test
expect "new hello: a failing test fails smith test" 1 "$rc"
expect_contains "new hello: ... and shows both values" "$out$err" "primes_sum(5) = 28"
capture smith check
expect "new hello: smith check fails when a test does" yes "$([ "$rc" -ne 0 ] && echo yes || echo no)"
git checkout -q -- tests/test_primes.c
capture smith check x
expect_contains "new hello: smith check takes no arguments" "$err" "takes no arguments"
capture smith build check
expect_contains "new hello: smith build check is refused for the verb" "$err" "check is a verb, not a target of smith build; run \`smith check\`"
capture smith check
expect "new hello: smith check passes on the new project" 0 "$rc"
if [ "$rc" -ne 0 ]; then echo "$err" | tail -n 30; fi
printf 'int   misformatted;\n' >>main.c
capture smith check
expect_contains "new hello: smith check fails on a misformatted file" "$out$err" "misformatted"
expect "new hello: ... with a nonzero status" yes "$([ "$rc" -ne 0 ] && echo yes || echo no)"
capture smith fmt
expect "new hello: smith fmt rewrites the sources" 0 "$rc"
expect_contains "new hello: ... to the project's style" "$(tail -n 1 main.c)" "int misformatted;"
cp main.c.orig main.c
rm main.c.orig
capture smith lint
expect "new hello: smith lint runs clang-tidy on the new project" 0 "$rc"
capture smith build # the log of the last build is the plain build's again

# The generated project passes the tools with the configs smith new wrote.
if "$smith" fmt --version >/dev/null 2>&1 && "$smith" lint --version >/dev/null 2>&1; then
    capture "$smith" fmt --dry-run --Werror main.c build.c
    expect "new hello: smith fmt passes on main.c and build.c" 0 "$rc"
    sdk=
    if [ "$(uname -s)" = Darwin ]; then sdk=$(xcrun --show-sdk-path); fi
    capture "$smith" lint --quiet main.c -- -std=gnu2y ${sdk:+-isysroot "$sdk"}
    expect "new hello: smith lint passes on main.c" 0 "$rc"
else
    echo "skip  new hello: smith fmt and smith lint (no clang-format and clang-tidy)"
fi
if "$smith" lsp --version >/dev/null 2>&1; then
    capture "$smith" lsp --check=main.c
    errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
    expect "new hello: clangd reports no errors in main.c" 0 "$errors"
fi
cd "$work" || exit 1

# 1b. assert(false) stops debug and release and not fast, in a scratch copy of the generated project.
mkdir abort && cp -R hello/build.c hello/primes.c hello/primes.h hello/tests abort/ && sed 's/assert(sum == 28);/assert(false);/' hello/main.c >abort/main.c
cd abort || exit 1
capture sh -c "smith build && smith build release fast"
expect "assert(false): the three build" 0 "$rc"
capture build/debug/hello
expect "assert(false): debug stops" 134 "$rc"
capture build/release/hello
expect "assert(false): release stops" 134 "$rc"
capture build/fast/hello
expect "assert(false): fast does not stop" 0 "$rc"
cd "$work" || exit 1

# 1c. smith's libraries build under the template's flags (gnu2y, every warning an error) in all three modes and run.
mkdir libs && cp -R hello/primes.c hello/primes.h hello/tests libs/
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
# The template's own build.c, with the two libraries added to its sources and -I .smith/include to its flags.
sed -e 's|{"main.c", "primes.c", nullptr}|{"main.c", "primes.c", ".smith/lib/arena.c", ".smith/lib/smith/random.smith", nullptr}|' \
    -e 's|(const char \*\[\]){"-ffp-contract=off", nullptr}|(const char *[]){"-ffp-contract=off", "-I", ".smith/include", nullptr}|' \
    hello/build.c >libs/build.c
cd libs || exit 1
capture sh -c "smith build && smith build release fast"
expect "libraries: the three build under the template's flags" 0 "$rc"
if [ "$rc" -ne 0 ]; then echo "$err" | head -n 20; fi
for mode in debug release fast; do
    capture build/$mode/hello
    expect "libraries: the $mode program runs" "libraries ok" "$out"
done
cd "$work" || exit 1

# 1d. The verbs read what the build declares, in a git copy of the generated project: prove proves the contracts of the
# sources that are compiled, fmt formats what git sees (and leaves out .gitignore and .clang-format-ignore), lint reports
# what clang-tidy finds, and check reports every part that fails.
mkdir verbs
(cd hello && tar cf - --exclude .git --exclude build --exclude .smith .) | (cd verbs && tar xf -)
cd verbs || exit 1
git init -q -b main && git add -A && git commit -q -m first
printf '#pragma once\n#include <stdint.h>\n\nint32_t clamp(int32_t x, int32_t low, int32_t high);\nint32_t draw_health(int32_t health, int32_t max_health);\nint32_t pinned(int32_t x);\n' >clamp.h
{ printf '#include "clamp.h"\n'; cat "$here/tests/prove/clamp.c"; } >clamp.c
sed -e 's|{"main.c", "primes.c", nullptr}|{"main.c", "primes.c", "clamp.c", nullptr}|' \
    -e 's|(const char \*\[\]){"-ffp-contract=off", nullptr}|(const char *[]){"-ffp-contract=off", "-I", ".smith/include", nullptr}|' \
    build.c >build.c.new && mv build.c.new build.c
capture smith prove
if [ "$(has "$err" "has no prover")" = yes ]; then
    echo "skip  new verbs: smith prove (this smith has no prover)"
else
    expect "new verbs: smith prove proves the contracts of the compiled sources" 0 "$rc"
    expect_contains "new verbs: ... both functions" "$out" "2 proved, 0 failed"
    printf '#include "clamp.h"\n\nint32_t pinned(int32_t x) {\n    return clamp(x, 10, 5);\n}\n' >bad_caller.c
    sed 's|"clamp.c", nullptr}|"clamp.c", "bad_caller.c", nullptr}|' build.c >build.c.new && mv build.c.new build.c
    capture smith prove
    expect "new verbs: a caller that breaks a contract fails smith prove" 1 "$rc"
    expect_contains "new verbs: ... at the call" "$out" "bad_caller.c:4: the call of clamp may break smith_requires(low <= high)"
    rm bad_caller.c
    sed 's|, "bad_caller.c"||' build.c >build.c.new && mv build.c.new build.c
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
git checkout -q -- .gitignore
printf 'int   BadName = 0;\n' >>primes.c
capture smith lint
expect "new verbs: smith lint fails on what clang-tidy finds" 1 "$rc"
expect_contains "new verbs: ... naming the check" "$out$err" "readability-identifier-naming"
expect_contains "new verbs: ... and how many files" "$err" "clang-tidy failed on 1 of 5 files"
capture smith check
expect "new verbs: smith check fails when parts do" 1 "$rc"
expect_contains "new verbs: ... names the parts that failed, in order" "$err" "smith check: failed: fmt lint"
expect_contains "new verbs: ... and the parts that passed" "$err" "smith check: test ok"
git checkout -q -- primes.c
cd "$work" || exit 1

# 2. A second run creates nothing.
before=$(tree_sum hello)
capture smith new hello
expect "second run: exits 0" 0 "$rc"
expect "second run: changes no bytes" "$before" "$(tree_sum hello)"
expect "second run: no new commit" 1 "$(commits hello)"
expect_contains "second run: everything is kept" "$out" "0 created, 10 kept"

# 3. No argument: the folder's name, and its own main.c kept.
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
cd "$work" || exit 1

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
expect "no identity: files written" yes "$(exists noid/proj/build.c)"
expect "no identity: files staged" yes "$(git -C noid/proj diff --cached --name-only | grep -q '^build.c$' && echo yes || echo no)"
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
expect "bad folder name: writes nothing" no "$(exists build.c)"
cd "$work" || exit 1
capture smith new one two
expect "two arguments: exit status" 2 "$rc"
expect_contains "two arguments: usage" "$err" "at most one argument"
expect "two arguments: creates nothing" no "$(exists one)"

# 9. An existing build.c is kept and gets no main.c; the first build is the project's own.
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
expect "existing build.c: no main.c" no "$(exists has_build/main.c)"
expect "existing build.c: the rules are written" yes "$(exists has_build/.clang-tidy)"
expect "existing build.c: the project's own build ran" yes "$(exists has_build/own.stamp)"

# 10. Another build system: build.c is a starting point, with no main.c and no first build.
mkdir has_make
printf 'all:\n\t@echo made\n' >has_make/Makefile
printf 'int main(void) { return 0; }\n' >has_make/other.c
capture smith new has_make
expect "Makefile: exits 0" 0 "$rc"
expect "Makefile: writes build.c" yes "$(exists has_make/build.c)"
expect "Makefile: no main.c" no "$(exists has_make/main.c)"
expect "Makefile: no first build" no "$(exists has_make/.smith)"
expect "Makefile: no concept about the program" no "$(exists has_make/knowledge/program.md)"
expect "Makefile: the index lists nothing but is there" "okf_version" \
    "$(sed -n 's/^\(okf_version\):.*/\1/p' has_make/knowledge/index.md)"
expect "Makefile: the index ends with its text, not a dangling item" yes "$(has "$(tail -n 1 has_make/knowledge/index.md)" "Read only the concepts")"
expect "Makefile: writes the rules" yes "$(exists has_make/.clang-format)"
expect "Makefile: writes the plugin settings" yes "$(exists has_make/.claude/settings.json)"
expect_contains "Makefile: the message names the Makefile" "$out" "This project builds with a Makefile."
expect_contains "Makefile: the message says to port it" "$out" "port the Makefile into it"
expect_contains "Makefile: the message names the skill" "$out" "smith-build skill"
expect "Makefile: commits only the files written" \
    ".agents/skills/has-make-docs/SKILL.md .clang-format .clang-tidy .claude/settings.json .claude/skills .gitignore AGENTS.md build.c knowledge/index.md" \
    "$(git -C has_make ls-files | tr '\n' ' ' | sed 's/ $//')"
capture smith docs has_make
expect "Makefile: the docs pass" 0 "$rc"
mkdir has_cmake
: >has_cmake/CMakeLists.txt
capture smith new has_cmake
expect_contains "CMakeLists.txt: the message names it" "$out" "This project builds with a CMakeLists.txt."
expect "CMakeLists.txt: no main.c" no "$(exists has_cmake/main.c)"

# 11. An existing .gitignore is kept; the lines it lacks are reported.
mkdir has_ignore
printf '/build/  \nnode_modules/\n' >has_ignore/.gitignore
capture smith new has_ignore
expect "gitignore: kept untouched" "$(printf '/build/  \nnode_modules/')" "$(cat has_ignore/.gitignore)"
expect_contains "gitignore: reports /compile_commands.json" "$out" "lacks /compile_commands.json"
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
    "$(missing_rules "$here/.clang-tidy" "$here/templates/new/clang-tidy")"
grep -v 'readability-function-size.LineThreshold' "$here/templates/new/clang-tidy" >"$work/tidy-without"
expect_contains "rules: a missing option is caught" "$(missing_rules "$here/.clang-tidy" "$work/tidy-without")" \
    "readability-function-size.LineThreshold"
grep -v 'misc-no-recursion' "$here/templates/new/clang-tidy" >"$work/tidy-without"
expect_contains "rules: a missing check is caught" "$(missing_rules "$here/.clang-tidy" "$work/tidy-without")" \
    "misc-no-recursion"
expect "rules: the template's .clang-format has smith's rules, comments aside" \
    "$(grep -v '^#' "$here/.clang-format")" "$(grep -v '^#' "$here/templates/new/clang-format")"

echo
echo "$total new checks, $failures failed"
[ "$failures" -eq 0 ]

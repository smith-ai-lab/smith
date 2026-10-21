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
ln -s "$smith" "$work/bin/smith" # the generated build.c names `smith` as its compiler
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
for f in build.c main.c .gitignore .clang-format .clang-tidy .claude/settings.json; do
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
expect "new hello: no .agents/skills folder" no "$(exists hello/.agents/skills)"
expect "new hello: no .claude/skills link" no "$(exists hello/.claude/skills)"
expect_contains "new hello: the summary names the plugin" "$new_out" "/plugin marketplace add adam-r-kowalski/smith"
expect_contains "new hello: the summary names the install" "$new_out" "/plugin install smith@smith"
expect "new hello: no missing-entry report" no "$(has "$new_out" 'lacks "smith@smith"')"
expect "new hello: git is on main" main "$(git -C hello symbolic-ref --short HEAD)"
expect "new hello: one commit" 1 "$(commits hello)"
expect "new hello: the commit message" "chore: create hello with smith new" "$(git -C hello log -1 --format=%s)"
expect "new hello: git tracks exactly the written files" \
    ".clang-format .clang-tidy .claude/settings.json .gitignore build.c main.c" \
    "$(git -C hello ls-files | tr '\n' ' ' | sed 's/ $//')"
expect "new hello: the tree is clean after the commit" "" "$(git -C hello status --porcelain)"
expect "new hello: the first build made the debug program" yes "$(exists hello/build/debug/hello)"
expect "new hello: the summary names the next step" yes "$(has "$new_out" "smith build run")"
cd hello || exit 1
capture smith build run
expect "new hello: smith build run prints the greeting" "hello from hello" "$(echo "$out" | tail -n 1)"
capture smith build release
expect "new hello: smith build release exits 0" 0 "$rc"
capture smith build fast
expect "new hello: smith build fast exits 0" 0 "$rc"
for mode in debug release fast; do
    expect "new hello: the $mode program exists" yes "$(exists build/$mode/hello)"
    expect "new hello: the $mode program runs and prints its line" "hello from hello" "$(build/$mode/hello | tail -n 1)"
done
expect "new hello: build.c turns modules on" yes "$(grep -q 'smith_modules(b' build.c && echo yes || echo no)"
expect "new hello: the builds compile with modules" 3 "$(grep -c "fmodules-cache-path=$XDG_CACHE_HOME/smith/modules" .smith/build.ninja)"
expect "new hello: the module cache is the shared one" yes "$(exists "$XDG_CACHE_HOME/smith/modules/modules.timestamp")"
expect "new hello: compile_commands.json has the module flags" yes "$(has "$(cat compile_commands.json)" "fmodule")"
expect "new hello: compile_commands has main.c" yes "$(grep -q '"main.c"' compile_commands.json && echo yes || echo no)"
expect "new hello: compile_commands has build.c" yes "$(grep -q '"build.c"' compile_commands.json && echo yes || echo no)"
debug_command=$(awk '/"file"/ { n++ } n == 1' compile_commands.json)
release_command=$(awk '/"file"/ { n++ } n == 2' compile_commands.json)
fast_command=$(awk '/"file"/ { n++ } n == 3' compile_commands.json)
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
expect "new hello: the build.c header lists release" yes "$(has "$(head -n 6 build.c)" "smith build release")"
expect "new hello: the build.c header lists fast" yes "$(has "$(head -n 6 build.c)" "smith build fast")"

# The generated project passes the tools with the configs smith new wrote.
llvm=${LLVM_BIN:-$(brew --prefix llvm 2>/dev/null || echo /opt/homebrew/opt/llvm)/bin}
if [ -x "$llvm/clang-format" ] && [ -x "$llvm/clang-tidy" ]; then
    capture "$llvm/clang-format" --dry-run --Werror main.c build.c
    expect "new hello: clang-format passes on main.c and build.c" 0 "$rc"
    sdk=
    if [ "$(uname -s)" = Darwin ]; then sdk=$(xcrun --show-sdk-path); fi
    capture "$llvm/clang-tidy" --quiet main.c -- -std=gnu2y ${sdk:+-isysroot "$sdk"}
    expect "new hello: clang-tidy passes on main.c" 0 "$rc"
else
    echo "skip  new hello: clang-format and clang-tidy (no LLVM at $llvm)"
fi
if [ -x /opt/homebrew/opt/llvm/bin/clangd ]; then
    capture /opt/homebrew/opt/llvm/bin/clangd --check=main.c
    errors=$(printf '%s\n%s\n' "$out" "$err" | grep -c 'E\[')
    expect "new hello: clangd reports no errors in main.c" 0 "$errors"
fi
cd "$work" || exit 1

# 1b. assert(false) stops debug and release and not fast, in a scratch copy of the generated project.
mkdir abort && cp hello/build.c abort/ && sed 's/assert(sum == 28);/assert(false);/' hello/main.c >abort/main.c
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
mkdir libs && cp hello/main.c libs/main.c.orig
cat >libs/insert.txt <<'C'
    SmithObject arena = {.compiler = "smith", .source = smith_library(b, "arena"),
                         .output = smith_fmt(b, "build/%s/arena.o", variant), .flags = include_flags};
    smith_object(b, &arena);
    SmithObject random = {.compiler = "smith", .source = smith_library(b, "random"),
                          .output = smith_fmt(b, "build/%s/random.o", variant), .flags = include_flags};
    smith_object(b, &random);
C
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
# The template's own build.c, with the two libraries compiled with each mode's flags and linked in.
sed -e 's|^    smith_object(b, &object);|&\n    const char *const *include_flags = smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr);\n@INSERT@|' \
    -e 's|\.flags = flags,|.flags = smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr),|' \
    -e 's|{&object, nullptr}|{\&object, \&arena, \&random, nullptr}|' hello/build.c |
    sed -e "/@INSERT@/r libs/insert.txt" -e '/@INSERT@/d' >libs/build.c
rm libs/insert.txt libs/main.c.orig
cd libs || exit 1
capture sh -c "smith build && smith build release fast"
expect "libraries: the three build under the template's flags" 0 "$rc"
if [ "$rc" -ne 0 ]; then echo "$err" | head -n 20; fi
for mode in debug release fast; do
    capture build/$mode/hello
    expect "libraries: the $mode program runs" "libraries ok" "$out"
done
cd "$work" || exit 1

# 2. A second run creates nothing.
before=$(tree_sum hello)
capture smith new hello
expect "second run: exits 0" 0 "$rc"
expect "second run: changes no bytes" "$before" "$(tree_sum hello)"
expect "second run: no new commit" 1 "$(commits hello)"
expect_contains "second run: everything is kept" "$out" "0 created, 5 kept"

# 3. No argument: the folder's name, and its own main.c kept.
mkdir demo && printf 'int main(void) { return 0; }\n' >demo/main.c
cd demo || exit 1
capture smith new
expect "no argument: exits 0" 0 "$rc"
expect "no argument: keeps main.c" "int main(void) { return 0; }" "$(cat main.c)"
expect "no argument: names the program demo" yes "$(exists build/debug/demo)"
expect "no argument: leaves out cd" no "$(has "$out" "cd demo")"
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
expect "Makefile: writes the rules" yes "$(exists has_make/.clang-format)"
expect "Makefile: writes the plugin settings" yes "$(exists has_make/.claude/settings.json)"
expect_contains "Makefile: the message names the Makefile" "$out" "This project builds with a Makefile."
expect_contains "Makefile: the message says to port it" "$out" "port the Makefile into it"
expect_contains "Makefile: the message names the skill" "$out" "smith-build skill"
expect "Makefile: commits only the files written" \
    ".clang-format .clang-tidy .claude/settings.json .gitignore build.c" \
    "$(git -C has_make ls-files | tr '\n' ' ' | sed 's/ $//')"
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

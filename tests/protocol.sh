#!/bin/sh
# Tests of the generated protocol code (knowledge/protocol-generation.md): runtime/protocol/dap.[ch] and mcp.[ch], which scripts/protocol.c
# writes from DAP's and MCP's schemas, and the tests it writes with them (tests/protocol/test_protocol_*.c: a round
# trip for every type and a refusal for every missing required member). Usage: sh tests/protocol.sh path/to/smith
# They are built in a scratch project with smith_tests, in two builds: -O0 under AddressSanitizer and UBSan, and -O2
# -DNDEBUG (which also stands for -O2: the optimizer's own warnings and the code without its assertions), with the
# strictest warnings smith uses, and run on 100 keys each. The 500-key run under the sanitizers is by hand: `./build/tests
# --seeds 500` in the asan build (issue #31: the check is CPU-bound, and these builds are most of this script).
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
if [ -n "${PROTOCOL_KEEP:-}" ]; then # to look at a failure: the scratch project is left in that folder
    trap - EXIT INT TERM
    rm -rf "$work"
    work=$PROTOCOL_KEEP
    rm -rf "$work" && mkdir -p "$work"
fi
export XDG_CACHE_HOME=$work/xdg-cache # the module and package caches of the tests, never the real ones
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
        printf '  expected: %s\n' "$2"
        printf '  actual:   %s\n' "$3"
        printf '%s\n%s\n' "$out" "$err"
    fi
}

mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
PATH="$work/bin:$PATH"

# The scratch project: smith's libraries the protocol code needs, the protocol code, and the runner.
mkdir -p "$work/project/tests" "$work/project/protocol" && cd "$work/project" || exit 1
cp "$here"/runtime/protocol/*.c "$here"/runtime/protocol/*.h protocol/
cp "$here"/tests/protocol/*.c "$here"/tests/protocol/*.h protocol/
rm protocol/test_protocol_*.c
cp "$here"/tests/protocol/test_protocol_*.c tests/
cat >build.c <<'C'
#include <smith/build.h>

#include <string.h>

void build(SmithBuild *b) {
    const char *mode = smith_option_value(b, "MODE", "asan", "asan, fast or ndebug");
    bool sanitize = strcmp(mode, "asan") == 0;
    const char *const *flags = smith_cat(
        b, smith_list(b, "-std=gnu23", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wconversion", "-Werror", "-I", ".smith/include", "-I", "protocol", nullptr),
        sanitize ? smith_list(b, "-O0", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", nullptr)
                 : smith_list(b, "-O2", nullptr),
        strcmp(mode, "ndebug") == 0 ? smith_list(b, "-DNDEBUG", nullptr) : smith_list(b, nullptr), nullptr);
    const char *const libraries[] = {"arena", "random", "text", "json", nullptr};
    const char *const sources[] = {"protocol/protocol.c", "protocol/protocol_support.c", "protocol/dap.c", "protocol/mcp.c", nullptr};
    SmithObject objects[8] = {};
    size_t count = 0;
    for (size_t i = 0; libraries[i] != nullptr; i++, count++) {
        objects[count] = (SmithObject){.compiler = "smith-cc", .source = smith_library(b, libraries[i]),
                                       .output = smith_fmt(b, "build/smith/%s.o", libraries[i]), .flags = flags};
        smith_object(b, &objects[count]);
    }
    for (size_t i = 0; sources[i] != nullptr; i++, count++) {
        objects[count] = (SmithObject){.compiler = "smith-cc", .source = sources[i],
                                       .output = smith_fmt(b, "build/protocol/%zu.o", i), .flags = flags};
        smith_object(b, &objects[count]);
    }
    const SmithObject *linked[9] = {};
    for (size_t i = 0; i < count; i++) linked[i] = &objects[i];
    SmithTests tests = {
        .sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)),
        .objects = linked,
        .flags = flags,
        .link_flags = smith_when(b, sanitize, smith_list(b, "-fsanitize=address,undefined", nullptr)),
    };
    smith_tests(b, &tests);
    smith_default(b, smith_list(b, "build/tests", nullptr));
}
C

summary_of() { printf '%s\n' "$out" | grep ' passed, ' | sed 's/ in [0-9.]* s$//'; }

count=$(cat tests/test_protocol_*.c | grep -c '^void test_')
for mode in asan ndebug; do
    capture smith build -DMODE=$mode
    expect "$mode: the generated code and its tests build" 0 "$rc"
    capture ./build/tests
    expect "$mode: all $count generated tests pass" "$count passed, 0 failed" "$(summary_of)"
    printf '%s\n' "$out" | grep ' passed, ' | sed 's/^/      /'
done

echo "$total tests, $failures failed"
[ "$failures" -eq 0 ]

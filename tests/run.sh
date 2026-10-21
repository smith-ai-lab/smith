#!/bin/sh
# Routing tests for smith, run against stub compilers so that no clang or Xcode is needed and no real build is touched.
# Usage: sh tests/run.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
mkdir "$work/bin"
for name in clang clang++ xcrun metal clang-format clang-tidy clangd; do
    $cc -std=gnu23 -DNAME="\"$name\"" "$here/stub.c" -o "$work/bin/$name" || exit 1
done
ln -s "$smith" "$work/smith-cc"
ln -s "$smith" "$work/smith-c++"
ln -s "$smith" "$work/bin/selfclang"
cl="$work/bin/clang"

failures=0
out=
err=
rc=0

# Runs a command with its stdout in $out, its stderr in $err and its exit status in $rc.
capture() {
    out=$("$@" 2>"$work/err")
    rc=$?
    err=$(cat "$work/err")
}

lines() {
    printf '%s\n' "$@"
}

expect() { # name, expected, actual
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected: $2"
        echo "  actual:   $3"
    fi
}

# The compiler is run in two forms, which must behave the same: the verbs `smith cc` and `smith c++`, and the links
# smith-cc and smith-c++ that act as them by their name.
compile_forms() {
    form=$1
    if [ "$form" = verb ]; then
        prog=$smith v=cc plus_prog=$smith plus_v=c++
    else
        prog=$work/smith-cc v= plus_prog=$work/smith-c++ plus_v=
    fi
    capture env SMITH_CLANG="$cl" "$prog" $v -c "a b.c" "" -DX="y z" -o out.o
    expect "[$form] clang route forwards every argument untouched" "$(lines "clang argv0=$cl" '[-c]' '[a b.c]' '[]' '[-DX=y z]' '[-o]' '[out.o]')" "$out"
    expect "[$form] clang route: stderr passes through" "clang-stderr" "$err"
    expect "[$form] clang route: status 0" 0 "$rc"

    capture env STUB_EXIT=7 SMITH_CLANG="$cl" "$prog" $v -c a.c
    expect "[$form] exit status is preserved" 7 "$rc"

    capture env STUB_SIGNAL=15 SMITH_CLANG="$cl" "$prog" $v -c a.c
    expect "[$form] death by signal is preserved (128 + 15)" 143 "$rc"

    capture env SMITH_CLANG="$cl" "$prog" $v --version
    expect "[$form] --version goes to the compiler" "$(lines "clang argv0=$cl" '[--version]')" "$out"

    capture env SMITH_CLANG="$cl" "$prog" $v @r.rsp a.c
    expect "[$form] response files are forwarded untouched" "$(lines "clang argv0=$cl" '[@r.rsp]' '[a.c]')" "$out"

    capture env SMITH_CLANG="$cl" "$prog" $v -I shaders.metal -c a.c
    expect "[$form] a flag value ending in .metal is not an input" "$(lines "clang argv0=$cl" '[-I]' '[shaders.metal]' '[-c]' '[a.c]')" "$out"

    capture env PATH="$work/bin:$PATH" "$prog" $v -o shaders.metallib a.metal b.metal
    expect "[$form] metal route goes through xcrun -sdk macosx metal" "$(lines "xcrun argv0=$work/bin/xcrun" '[-sdk]' '[macosx]' '[metal]' '[-o]' '[shaders.metallib]' '[a.metal]' '[b.metal]')" "$out"

    capture env SMITH_METAL="$work/bin/metal" "$prog" $v -E a.metal
    expect "[$form] SMITH_METAL replaces xcrun" "$(lines "metal argv0=$work/bin/metal" '[-E]' '[a.metal]')" "$out"

    capture env SMITH_CLANG="$cl" "$prog" $v a.metal b.c
    expect "[$form] mixed .metal and C inputs are refused" "smith: .metal and other inputs need different compilers and flags; compile the shaders in their own command" "$err"
    expect "[$form] mixed inputs: status 2 and nothing ran" "2 " "$rc $out"

    capture env SMITH_CLANG="$cl" "$prog" $v -c a.smith -o out.o
    expect "[$form] a .smith input goes to clang with its arguments untouched" "$(lines "clang argv0=$cl" '[-c]' '[a.smith]' '[-o]' '[out.o]')" "$out"

    capture env SMITH_CLANGXX="$work/bin/clang++" "$plus_prog" $plus_v -c a.cpp
    expect "[$form] smith++ uses clang++" "$(lines "clang++ argv0=$work/bin/clang++" '[-c]' '[a.cpp]')" "$out"

    in_process=no
    if [ "$(uname -s)" = Darwin ]; then
        capture env -u SMITH_CLANG "$prog" $v --smith-which -c a.c
        which_identity=$(printf '%s\n' "$out" | tail -n +2)
        out=$(printf '%s\n' "$out" | head -n 1)
        case $out in
        "clang in-process "/*/bin/clang)
            # A smith with clang linked in compiles in its own process unless SMITH_CLANG names a clang to run instead.
            llvm_clang=${out#clang in-process }
            # The program is in the folder smith makes for itself on first use (named for its LC_UUID), which --smith-which does not make.
            case $llvm_clang in
            */smith/programs/*/bin/clang) found=yes ;;
            *) found=$llvm_clang ;;
            esac
            expect "[$form] in-process: the default is clang inside smith, run as the bin/clang of a folder of its own" yes "$found"
            case $which_identity in
            "identity llvm-"*" "*) expect "[$form] in-process: --smith-which names the identity of the clang inside" yes yes ;;
            *) expect "[$form] in-process: --smith-which names the identity of the clang inside" "identity llvm-<version> <hash>" "$which_identity" ;;
            esac
            capture env -u SMITH_CLANGXX "$plus_prog" $plus_v --smith-which -c a.cpp
            expect "[$form] in-process: smith++ is clang++ inside smith" "clang in-process ${llvm_clang}++" "$(printf '%s\n' "$out" | head -n 1)"
            capture env SMITH_CLANG="$cl" "$prog" $v --smith-which -c a.c
            expect "[$form] in-process: SMITH_CLANG still runs an external clang" "clang $cl" "$out"
            capture env -u SMITH_CLANG SMITH_CLANGXX="$work/bin/clang++" "$prog" $v --smith-which -c a.c
            expect "[$form] in-process: SMITH_CLANGXX does not affect smith" "clang in-process" "${out%% /*}"
            in_process=yes ;;
        *)
            if [ -x /opt/homebrew/opt/llvm/bin/clang ]; then
                expect "[$form] on macOS the default is Homebrew's clang" "clang /opt/homebrew/opt/llvm/bin/clang" "$out"
                capture env -u SMITH_CLANGXX "$plus_prog" $plus_v --smith-which -c a.cpp
                expect "[$form] on macOS smith++ defaults to Homebrew's clang++" "clang /opt/homebrew/opt/llvm/bin/clang++" "$out"
            fi
            if [ ! -x /opt/homebrew/opt/llvm/bin/clang ] && [ ! -x /usr/local/opt/llvm/bin/clang ]; then
                capture env -u SMITH_CLANG "$prog" $v -c a.c
                expect "[$form] macOS without Homebrew's llvm: the error names brew install llvm and SMITH_CLANG" \
                    "smith: cannot find clang in /opt/homebrew/opt/llvm/bin: run \`brew install llvm\`, or set SMITH_CLANG" "$err"
            fi ;;
        esac
        # The SDK is chosen through the environment, never a flag, and only for what the caller left unset.
        capture env -u SDKROOT -u CLANG_NO_DEFAULT_CONFIG PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_SHOW_ENV=1 \
            SMITH_CLANG="$cl" "$prog" $v -c a.c
        expect "[$form] macOS: SDKROOT comes from xcrun and the default config is off, with no flag added" \
            "$(lines 'env SDKROOT=/stub/sdk CLANG_NO_DEFAULT_CONFIG=1' "clang argv0=$cl" '[-c]' '[a.c]')" "$out"
        capture env -u CLANG_NO_DEFAULT_CONFIG SDKROOT=/mine PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_SHOW_ENV=1 \
            SMITH_CLANG="$cl" "$prog" $v -c a.c
        expect "[$form] macOS: the caller's SDKROOT wins, and the default config is still off" \
            "$(lines 'env SDKROOT=/mine CLANG_NO_DEFAULT_CONFIG=1' "clang argv0=$cl" '[-c]' '[a.c]')" "$out"
        capture env -u SDKROOT CLANG_NO_DEFAULT_CONFIG=0 PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_SHOW_ENV=1 \
            SMITH_CLANG="$cl" "$prog" $v -c a.c
        expect "[$form] macOS: the caller's CLANG_NO_DEFAULT_CONFIG wins" \
            "$(lines 'env SDKROOT=/stub/sdk CLANG_NO_DEFAULT_CONFIG=0' "clang argv0=$cl" '[-c]' '[a.c]')" "$out"
        capture env -u SDKROOT -u CLANG_NO_DEFAULT_CONFIG PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_XCRUN_FAIL=1 \
            STUB_SHOW_ENV=1 SMITH_CLANG="$cl" "$prog" $v -c a.c
        expect "[$form] macOS: when xcrun fails SDKROOT stays unset" \
            "$(lines 'env SDKROOT=(unset) CLANG_NO_DEFAULT_CONFIG=1' "clang argv0=$cl" '[-c]' '[a.c]')" "$out"
        capture env -u SDKROOT -u CLANG_NO_DEFAULT_CONFIG PATH="$work/bin:$PATH" STUB_SDK=/stub/sdk STUB_SHOW_ENV=1 \
            SMITH_CLANG="$cl" "$prog" $v -isysroot /mine/sdk -c a.c
        expect "[$form] macOS: the caller's -isysroot is forwarded untouched" \
            "$(lines 'env SDKROOT=/stub/sdk CLANG_NO_DEFAULT_CONFIG=1' "clang argv0=$cl" '[-isysroot]' '[/mine/sdk]' '[-c]' '[a.c]')" "$out"
    fi
}

for form in verb link; do compile_forms $form; done

# fmt, lint and lsp are clang-format, clang-tidy and clangd with the arguments untouched, as the exact first argument.
for tool in fmt:clang-format:SMITH_CLANG_FORMAT lint:clang-tidy:SMITH_CLANG_TIDY lsp:clangd:SMITH_CLANGD; do
    command=${tool%%:*}
    rest=${tool#*:}
    program=${rest%%:*}
    variable=${rest#*:}
    stub=$work/bin/$program
    capture env "$variable=$stub" "$smith" "$command" -i "a b.c" "" --style=file -DX="y z"
    expect "$command: runs $program with every argument untouched" \
        "$(lines "$program argv0=$stub" '[-i]' '[a b.c]' '[]' '[--style=file]' '[-DX=y z]')" "$out"
    expect "$command: stderr passes through" "$program-stderr" "$err"
    capture env STUB_EXIT=3 "$variable=$stub" "$smith" "$command" a.c
    expect "$command: exit status is $program's" 3 "$rc"
    capture env STUB_SIGNAL=15 "$variable=$stub" "$smith" "$command" a.c
    expect "$command: death by signal is preserved (128 + 15)" 143 "$rc"
    capture env "$variable=/nonexistent/$program" "$smith" "$command" a.c
    expect "$command: a missing tool is reported" "smith: not an executable: /nonexistent/$program" "$err"
    # Only as the exact first argument: after another argument it is a file name, for the compiler.
    capture env SMITH_CLANG="$cl" "$variable=$stub" "$smith" cc -c "$command" a.c
    expect "$command: after another argument it goes to clang" "$(lines "clang argv0=$cl" '[-c]' "[$command]" '[a.c]')" "$out"
done
if [ "$in_process" = yes ]; then
    # The tools are inside smith: the same LLVM as the compiler, run on the arguments, in smith's process.
    mkdir "$work/tools"
    printf 'int   main( ){return  0;}\n' >"$work/tools/a.c"
    capture env -u SMITH_CLANG_FORMAT "$smith" fmt --version
    case $out in
    "clang-format version 23."*) expect "in-process: fmt is clang-format 23" yes yes ;;
    *) expect "in-process: fmt is clang-format 23" "clang-format version 23." "$out" ;;
    esac
    capture env -u SMITH_CLANG_FORMAT "$smith" fmt --style=LLVM "$work/tools/a.c"
    expect "in-process: fmt formats a file to stdout" "$(lines 'int main() { return 0; }')" "$out"
    printf 'IndentWidth: 8\nUseTab: Never\nAllowShortFunctionsOnASingleLine: None\n' >"$work/tools/.clang-format"
    printf 'int main() {\nreturn 0;\n}\n' >"$work/tools/b.c"
    capture env -u SMITH_CLANG_FORMAT "$smith" fmt "$work/tools/b.c"
    expect "in-process: fmt reads the project's .clang-format" "$(lines 'int main() {' '        return 0;' '}')" "$out"
    capture env -u SMITH_CLANG_FORMAT "$smith" fmt -i "$work/tools/b.c"
    expect "in-process: fmt -i rewrites the file" "$(lines 'int main() {' '        return 0;' '}')" "$(cat "$work/tools/b.c")"
    capture env -u SMITH_CLANG_TIDY "$smith" lint --version
    case $out in
    *"LLVM version 23."*) expect "in-process: lint is clang-tidy 23" yes yes ;;
    *) expect "in-process: lint is clang-tidy 23" "LLVM version 23." "$out" ;;
    esac
    capture env -u SMITH_CLANG_TIDY "$smith" lint --list-checks --checks='-*,readability-*'
    case $out in
    *"readability-identifier-naming"*) expect "in-process: lint lists clang-tidy's checks" yes yes ;;
    *) expect "in-process: lint lists clang-tidy's checks" "readability-identifier-naming" "$out" ;;
    esac
    printf 'Checks: "-*,readability-uppercase-literal-suffix"\nWarningsAsErrors: "*"\n' >"$work/tools/.clang-tidy"
    printf 'constexpr float x = 1.0f;\n' >"$work/tools/c.c"
    capture env -u SMITH_CLANG_TIDY "$smith" lint --quiet "$work/tools/c.c" -- -std=gnu23
    case $out in
    *"floating point literal has suffix 'f'"*) expect "in-process: lint reads the project's .clang-tidy and reports" yes yes ;;
    *) expect "in-process: lint reads the project's .clang-tidy and reports" "floating point literal has suffix" "$out" ;;
    esac
    expect "in-process: lint exits 1 on a finding that is an error" 1 "$rc"
    capture env -u SMITH_CLANGD "$smith" lsp --version
    case $out in
    "clangd version 23."*) expect "in-process: lsp is clangd 23" yes yes ;;
    *) expect "in-process: lsp is clangd 23" "clangd version 23." "$out" ;;
    esac
fi


compile_tail() {
    form=$1
    if [ "$form" = verb ]; then prog=$smith v=cc; else prog=$work/smith-cc v=; fi
    capture env SMITH_CLANG="$work/bin/selfclang" "$prog" $v a.c
    expect "[$form] a compiler that is smith itself is refused" "smith: refers to smith itself: $work/bin/selfclang" "$err"
    expect "[$form] self refusal: status 127" 127 "$rc"

    capture env SMITH_CLANG=/nonexistent/clang "$prog" $v a.c
    expect "[$form] a missing compiler is reported" "smith: not an executable: /nonexistent/clang" "$err"

    capture env SMITH_CLANG="$cl" "$prog" $v --smith-which -c a.c
    expect "[$form] --smith-which names the clang backend" "clang $cl" "$out"

    capture env PATH="$work/bin:$PATH" "$prog" $v --smith-which a.metal
    expect "[$form] --smith-which names the metal backend" "metal $work/bin/xcrun -sdk macosx metal" "$out"

    capture env SMITH_VERBOSE=1 SMITH_CLANG="$cl" "$prog" $v -c a.c
    expect "[$form] SMITH_VERBOSE shows the command" "$(lines "smith: exec $cl -c a.c" 'clang-stderr')" "$err"
}

for form in verb link; do compile_tail $form; done

# The top level is only verbs: the compiler's arguments with no verb are an error that says what to run.
capture env SMITH_CLANG="$cl" "$smith" foo.c -o foo
expect "bare: the compiler's arguments are an error with the hint" \
    "smith: foo.c is not a smith command; to compile, run \`smith cc foo.c -o foo\`" "$err"
expect "bare: status 2 and nothing ran" "2 " "$rc $out"
capture env SMITH_CLANG="$cl" "$smith" -c a.c
expect "bare: a flag first gets the same hint" "smith: -c is not a smith command; to compile, run \`smith cc -c a.c\`" "$err"
capture env SMITH_CLANG="$cl" "$smith" --smith-which -c a.c
expect "bare: --smith-which is a flag of the compiler verbs" \
    "smith: --smith-which is not a smith command; to compile, run \`smith cc --smith-which -c a.c\`" "$err"
ln -s "$smith" "$work/smith++"
capture env SMITH_CLANG="$cl" "$work/smith++" -c a.cpp
expect "bare: the old smith++ name is no compiler" "smith: -c is not a smith command; to compile, run \`smith cc -c a.cpp\`" "$err"
capture "$smith"
expect "no arguments: usage, status 2" "2 usage: smith <command> [<arguments>]" "$rc $(printf '%s\n' "$err" | head -n 1)"
capture env SMITH_CLANG="$cl" "$smith" cc
expect "smith cc with no arguments is clang with none" "$(lines "clang argv0=$cl")" "$out"

# The driver runs its own program again with -cc1, as bin/clang and bin/clang++ of the programs folder, which are links
# to smith: those names act as the compiler, with the arguments as clang's.
mkdir "$work/driver"
ln -s "$smith" "$work/driver/clang"
ln -s "$smith" "$work/driver/clang++"
capture env SMITH_CLANG="$cl" "$work/driver/clang" -c a.c
expect "the name clang acts as smith cc" "$(lines "clang argv0=$cl" '[-c]' '[a.c]')" "$out"
capture env SMITH_CLANGXX="$work/bin/clang++" "$work/driver/clang++" -c a.cpp
expect "the name clang++ acts as smith c++" "$(lines "clang++ argv0=$work/bin/clang++" '[-c]' '[a.cpp]')" "$out"

# prove and dbg are not available yet: they say so, and name the issue.
capture "$smith" prove
expect "prove: not available yet" "smith: prove is not available yet (issue #8)" "$err"
capture "$smith" dbg
expect "dbg: not available yet" "smith: dbg is not available yet (issue #7)" "$err"

# fmt and lint with no arguments, or only -D options of the build, are the project's verbs; the folder has no build.c.
mkdir "$work/empty"
for verb in fmt lint run test check; do
    capture sh -c "cd '$work/empty' && '$smith' $verb -DX=1"
    expect "$verb -DX=1: the project's verb, which needs a build.c" "smith: no build.c in this folder" "$err"
done

# A project verb is a verb, not a target of smith build.
for verb in run test check fmt lint prove dbg; do
    capture "$smith" build "$verb"
    expect "build $verb: refused, with a pointer to the verb" \
        "smith: $verb is a verb, not a target of smith build; run \`smith $verb\`" "$err"
done

echo
if [ "$failures" -eq 0 ]; then echo "all routing tests passed"; else echo "$failures failed"; fi
[ "$failures" -eq 0 ]

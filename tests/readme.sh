#!/bin/sh
# README.md shows only what smith does today (issue #142): every code block of the README is compiled, and where marked
# run, tested or proved. A block's info string says what it is:
#   smith [project=NAME] [file=PATH] [flags]   a .smith file. Blocks with the same project=NAME are one folder (a block
#                                              with no project is a folder of its own); the file is the one the `module`
#                                              line implies (module a::b is a/b.smith), or file=PATH.
#   c, make [project=NAME] [file=PATH] [flags] a C file (default: checked alone with -fsyntax-only) or a Makefile.
#   base=NAME                                  (a flag of a project's first block) starts the project from the files of
#                                              project NAME, which came before it; the project's own blocks replace theirs.
#   output project=NAME                        the text `smith run` of that project prints, stdout only, exactly.
#   anything else (sh, text)                   shown, not checked (its commands are checked by the flags below).
# Flags: run (`smith run` must succeed and print the project's output block, if it has one), test (`smith test`),
#   prove (`smith prove` must pass), refuse (`smith prove` must fail and print every line of the project's output block),
#   port (the project is a C project with a Makefile: `CC=smith-cc make` builds it, `smith build --dump-config > build.c`
#   translates it, `smith build` builds that and prints what make's program printed). A project with none of these is
#   compiled only: its headers are made and each file is syntax-checked with warnings as errors, as tests/skills.sh does.
# A block inside an HTML comment is checked and not shown. Also checked: every relative link of the README exists, and
# every complete smith block passes smith's own idiom checks (no #include or #define, no standard C type names).
# A smith without clang inside (the stage0), or whose clang lacks the token hook, is skipped.
# Usage: sh tests/readme.sh path/to/smith [path/to/README.md]
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(pwd)
readme=${2:-$here/README.md}
work=$(mktemp -d)
if [ -n "${README_KEEP:-}" ]; then echo "the folders are kept in $work"; else trap 'rm -rf "$work"' EXIT INT TERM; fi
export XDG_DATA_HOME=$work/data
export XDG_CACHE_HOME=$work/cache
mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
export PATH=$work/bin:$PATH

if [ "$(smith cc --smith-which -c a.c | head -n 1 | cut -d ' ' -f 1,2)" != "clang in-process" ]; then
    echo "skipped: this smith has no clang inside (the stage0)"
    exit 0
fi
printf 'int main(void) { return 0; }\n' >"$work/probe.smith"
if smith cc --smith-lower "$work/probe.smith" 2>&1 | grep -q "token hook"; then
    echo "skipped: this smith's clang lacks the token hook"
    exit 0
fi

failures=0
total=0
fail() { # name, details file
    failures=$((failures + 1))
    echo "FAIL  $1"
    if [ -n "${2:-}" ] && [ -f "$2" ]; then head -n 20 "$2" | sed 's/^/        /'; fi
}
pass() { total=$((total + 1)); echo "ok    $1"; }

# One awk pass: block-N holds each block's text, `index` has "N kind project file flags line" for each.
mkdir -p "$work/raw"
awk -v dir="$work/raw" -v index_file="$work/index" '
    BEGIN { open = 0; n = 0 }
    {
        line = $0
        if (match(line, /^[ ]*```/)) {
            if (!open) {
                info = substr(line, RLENGTH + 1)
                sub(/[ \t]+$/, "", info)
                open = 1; n++
                count = split(info, words, " ")
                kind = words[1]; project = "block-" n; path = "-"; flags = ""
                for (i = 2; i <= count; i++) {
                    if (words[i] ~ /^project=/) project = substr(words[i], 9)
                    else if (words[i] ~ /^file=/) path = substr(words[i], 6)
                    else flags = flags "," words[i]
                }
                if (flags == "") flags = ","
                file = dir "/" n
                printf "%d %s %s %s %s %d\n", n, kind, project, path, flags, NR + 1 >> index_file
                printf "" > file
            } else {
                open = 0; close(file)
            }
            next
        }
        if (open) print line >> file
    }
' "$readme"
[ -f "$work/index" ] || { echo "FAIL  README.md has no code block"; exit 1; }

# Place every block in the folder of its project.
while read -r n kind project path flags lineno; do
    raw=$work/raw/$n
    dir=$work/p/$project
    mkdir -p "$dir"
    case $flags in
    *,base=*)
        # A project that starts from another's files (its own blocks replace theirs).
        base=${flags#*,base=}
        base=${base%%,*}
        if [ ! -f "$dir/.based" ]; then
            cp -R "$work/p/$base"/* "$dir"/
            : >"$dir/.based"
        fi
        ;;
    esac
    case $kind in
    output) cat "$raw" >>"$dir/.expected" ;;
    smith | c | make)
        if [ "$path" = - ]; then
            module=$(sed -n 's/^module \([A-Za-z0-9_:]*\);.*/\1/p' "$raw" | head -n 1)
            case $kind in
            make) path=Makefile ;;
            c) path=c-$n.c ;;
            *) if [ -n "$module" ]; then path=$(printf '%s' "$module" | sed 's|::|/|g').smith; else path=example-$n.smith; fi ;;
            esac
        fi
        mkdir -p "$(dirname "$dir/$path")"
        if grep -q "^$path " "$dir/.files" 2>/dev/null; then
            echo "$path is written by two blocks" >"$work/msg"
            fail "README.md:$((lineno - 1)): $project/$path twice" "$work/msg"
            continue
        fi
        cp "$raw" "$dir/$path"
        echo "$path $kind $n $lineno" >>"$dir/.files"
        for flag in $(printf '%s' "$flags" | tr ',' ' '); do : >"$dir/.flag-$flag"; done
        if [ "$kind" = smith ] &&
            grep -nE '^[[:space:]]*#[[:space:]]*(include|define)|\<(u?int(8|16|32|64)_t|size_t|ptrdiff_t)\>' "$raw" >"$work/msg"; then
            fail "README.md:$((lineno - 1)): a smith block is C renamed (see smith-idiom)" "$work/msg"
        fi
        ;;
    esac
done <"$work/index"

has() { [ -f "$1/.flag-$2" ]; }

for dir in "$work"/p/*/; do
    dir=${dir%/}
    project=$(basename "$dir")
    [ -f "$dir/.files" ] || continue
    first=$(head -n 1 "$dir/.files" | cut -d ' ' -f 4)
    name="README.md:$((first - 1)) ($project)"
    cd "$dir" || exit 1
    # The code is what smith fmt writes, and passes smith lint's checks (a project: all of them).
    while read -r path kind n lineno; do
        [ "$kind" = smith ] || continue
        if smith fmt --dry-run --Werror "$dir/$path" >"$work/out" 2>&1; then :; else fail "README.md:$((lineno - 1)) ($path): not what smith fmt writes" "$work/out"; fi
    done <"$dir/.files"
    if has "$dir" port; then
        if CC=smith-cc make >"$work/out" 2>&1 && ./app >"$work/printed" 2>&1 &&
            smith build --dump-config >build.c 2>"$work/out" && rm -f app ./*.o && smith build >"$work/out" 2>&1 &&
            ./app >"$work/stdout" 2>&1 && diff "$work/printed" "$work/stdout" >"$work/out"; then
            pass "$name: port (make with smith-cc, --dump-config, build)"
        else
            fail "$name: port" "$work/out"
        fi
        continue
    fi
    if has "$dir" rejects; then
        if smith run >"$work/out" 2>&1; then
            echo "smith run succeeded; the README says it is an error" >"$work/out"
            fail "$name: rejects" "$work/out"
        else
            : >"$work/diff"
            while IFS= read -r wanted; do
                [ -n "$wanted" ] || continue
                grep -qF -- "$wanted" "$work/out" || echo "not in the error: $wanted" >>"$work/diff"
            done <.expected
            if [ ! -s "$work/diff" ]; then pass "$name: rejects"; else cat "$work/out" >>"$work/diff"; fail "$name: rejects" "$work/diff"; fi
        fi
        continue
    fi
    if has "$dir" run || has "$dir" test || has "$dir" prove || has "$dir" refuse; then
        if has "$dir" run; then
            # smith run prints the build's progress lines ("[3/12] compile ...") before the program's own output.
            if smith lint --quiet >"$work/out" 2>&1 && smith run >"$work/progress" 2>"$work/out"; then
                sed '/^\[[0-9]*\/[0-9]*\] /d' "$work/progress" >"$work/stdout"
                if [ ! -f .expected ] || diff .expected "$work/stdout" >"$work/diff"; then
                    pass "$name: run"
                else
                    fail "$name: run printed other than its output block" "$work/diff"
                fi
            else
                fail "$name: run" "$work/out"
            fi
        fi
        if has "$dir" test; then
            if smith test >"$work/out" 2>&1; then pass "$name: test"; else fail "$name: test" "$work/out"; fi
        fi
        if has "$dir" prove; then
            if smith prove >"$work/out" 2>&1; then pass "$name: prove"; else fail "$name: prove" "$work/out"; fi
        fi
        if has "$dir" refuse; then
            if smith prove >"$work/out" 2>&1; then
                echo "smith prove passed; the README says it refuses" >"$work/out"
                fail "$name: refuse" "$work/out"
            else
                missing=0
                while IFS= read -r wanted; do
                    [ -n "$wanted" ] || continue
                    grep -qF -- "$wanted" "$work/out" || { missing=1; echo "not in the output: $wanted" >>"$work/diff"; }
                done <.expected
                if [ "$missing" -eq 0 ]; then pass "$name: refuse"; else fail "$name: refuse" "$work/diff"; fi
            fi
        fi
        continue
    fi
    # Compiled only: the headers of the modules, twice (the second pass has the headers the first made), then each file.
    mkdir -p .smith/include
    flags="-I $here/include -I $dir/.smith/include -I $dir"
    while read -r path kind n lineno; do
        if [ "$kind" = c ]; then
            # shellcheck disable=SC2086
            if smith cc -fsyntax-only -std=gnu23 -Wall -Wextra -Werror "$dir/$path" >"$work/out" 2>&1; then
                pass "README.md:$((lineno - 1)) (c)"
            else
                fail "README.md:$((lineno - 1)) (c)" "$work/out"
            fi
        fi
    done <.files
    for pass in 1 2; do
        while read -r path kind n lineno; do
            [ "$kind" = smith ] || continue
            module=$(sed -n 's/^module \([A-Za-z0-9_:]*\);.*/\1/p' "$dir/$path" | head -n 1)
            [ -n "$module" ] || continue
            header=$dir/.smith/include/$(printf '%s' "$module" | sed 's|::|/|g').h
            mkdir -p "$(dirname "$header")"
            # shellcheck disable=SC2086
            smith cc --smith-header "$dir/$path" -o "$header" $flags >/dev/null 2>&1 || :
        done <.files
    done
    while read -r path kind n lineno; do
        [ "$kind" = smith ] || continue
        # shellcheck disable=SC2086
        if smith cc -fsyntax-only -std=gnu2y -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wvla -Wno-unused-function -Wno-unused-variable -Werror $flags "$dir/$path" >"$work/out" 2>&1 &&
            smith lint --quiet "$dir/$path" -- -std=gnu2y $flags >"$work/out" 2>&1; then
            pass "README.md:$((lineno - 1)) ($path)"
        else
            fail "README.md:$((lineno - 1)) ($path)" "$work/out"
        fi
    done <.files
done
cd "$here" || exit 1

# Every relative link of the README names a file that exists.
links=0
for target in $(grep -o '](\([^)#]*\)' "$readme" | sed 's/^](//' | grep -v '^[a-z]*:' | sort -u); do
    links=$((links + 1))
    if [ -e "$here/$target" ]; then :; else echo "FAIL  README.md links to $target: no such file"; failures=$((failures + 1)); fi
done

echo "$total checks ok, $failures failed, $links links"
[ "$failures" -eq 0 ]

#!/bin/sh
# The examples in the plugin's skills (.agents/skills/*/SKILL.md) are compiled, not trusted. Every fenced block whose
# info string is exactly `smith` is a complete file: it is written to a folder of its own per skill (a block that starts
# `module a::b;` goes to a/b.smith, as the path rule needs; the others to example-N.smith), the headers of the skill's
# modules are made (`smith cc --smith-header`, as `smith build` does, twice so that a module may import another), and
# every file is syntax-checked with `smith cc -fsyntax-only` and warnings as errors. A block marked `smith fragment` is
# part of a file and is skipped; C is `c`. Also checked, so that an example teaches the idiom: a complete `smith` block
# has no #include, #define, or standard C type (uint32_t, size_t...), and a `c` block has no `module`, `import` or `c::`.
# A smith without clang inside (the stage0), or whose clang lacks the token hook, is skipped.
# Usage: sh tests/skills.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
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
fragments=0
fail() { # name, details file
    failures=$((failures + 1))
    echo "FAIL  $1"
    if [ -n "${2:-}" ] && [ -f "$2" ]; then head -n 20 "$2" | sed 's/^/        /'; fi
}

# One awk pass per skill: writes block-N.smith (smith), c-N.txt (c) and index lines "kind N first-line-of-block".
for skill in "$here"/.agents/skills/*/SKILL.md; do
    name=$(basename "$(dirname "$skill")")
    dir=$work/$name
    mkdir -p "$dir/raw" "$dir/.smith/include"
    awk -v dir="$dir/raw" -v index_file="$dir/index" '
        BEGIN { open = 0; n = 0 }
        {
            line = $0
            if (match(line, /^[ ]*```/)) {
                indent = RLENGTH - 3
                if (!open) {
                    info = substr(line, RLENGTH + 1)
                    sub(/[ \t]+$/, "", info)
                    open = 1; n++; kind = "other"
                    if (info == "smith") kind = "smith"
                    else if (info == "smith fragment") kind = "fragment"
                    else if (info == "c") kind = "c"
                    else if (info ~ /^smith/) kind = "badinfo"
                    file = dir "/" n
                    printf "%s %d %d %s\n", kind, n, NR + 1, info >> index_file
                    printf "" > file
                } else {
                    open = 0; close(file)
                }
                next
            }
            if (open) {
                if (indent > 0) sub("^[ ]{0," indent "}", "", line)
                print line >> file
            }
        }
    ' "$skill"
    [ -f "$dir/index" ] || continue

    # Place the complete blocks.
    : >"$dir/modules"
    while read -r kind n lineno info; do
        raw=$dir/raw/$n
        case $kind in
        badinfo)
            total=$((total + 1))
            echo "info string" >"$dir/msg"
            echo "must be exactly 'smith' or 'smith fragment', not '$info'" >>"$dir/msg"
            fail "$name/SKILL.md:$((lineno - 1)): unknown info string" "$dir/msg"
            ;;
        fragment) fragments=$((fragments + 1)) ;;
        c)
            total=$((total + 1))
            if grep -nE '^(module|import) |c::' "$raw" >"$dir/msg"; then
                fail "$name/SKILL.md:$((lineno - 1)): a c block holds .smith (tag it smith)" "$dir/msg"
            else
                echo "ok    $name/SKILL.md:$((lineno - 1)) (c)"
            fi
            ;;
        smith)
            if grep -nE '^[[:space:]]*#[[:space:]]*(include|define)|\<(u?int(8|16|32|64)_t|size_t|ptrdiff_t)\>' "$raw" >"$dir/msg"; then
                total=$((total + 1))
                fail "$name/SKILL.md:$((lineno - 1)): a smith block is C renamed (see smith-idiom)" "$dir/msg"
                continue
            fi
            module=$(sed -n 's/^module \([A-Za-z0-9_:]*\);.*/\1/p' "$raw" | head -n 1)
            if [ -n "$module" ]; then
                path=$(printf '%s' "$module" | sed 's|::|/|g').smith
            else
                path=example-$n.smith
            fi
            mkdir -p "$(dirname "$dir/$path")"
            if [ -f "$dir/$path" ]; then
                total=$((total + 1))
                echo "module $module is defined by two blocks" >"$dir/msg"
                fail "$name/SKILL.md:$((lineno - 1)): module defined twice" "$dir/msg"
                continue
            fi
            cp "$raw" "$dir/$path"
            echo "$path $lineno" >>"$dir/modules"
            ;;
        esac
    done <"$dir/index"
    [ -s "$dir/modules" ] || continue

    # The headers of the skill's modules, twice: the second pass has the headers the first made.
    flags="-I $here/include -I $dir/.smith/include -I $dir"
    for pass in 1 2; do
        while read -r path lineno; do
            module=$(sed -n 's/^module \([A-Za-z0-9_:]*\);.*/\1/p' "$dir/$path" | head -n 1)
            [ -n "$module" ] || continue
            header=$dir/.smith/include/$(printf '%s' "$module" | sed 's|::|/|g').h
            mkdir -p "$(dirname "$header")"
            # shellcheck disable=SC2086
            smith cc --smith-header "$dir/$path" -o "$header" $flags >"$dir/header.$pass" 2>&1 || :
        done <"$dir/modules"
    done

    while read -r path lineno; do
        total=$((total + 1))
        # shellcheck disable=SC2086
        if smith cc -fsyntax-only -std=gnu2y -Wall -Wextra -Wshadow -Wconversion -Werror $flags "$dir/$path" >"$dir/out" 2>&1; then
            echo "ok    $name/SKILL.md:$((lineno - 1)) ($path)"
        else
            fail "$name/SKILL.md:$((lineno - 1)) ($path)" "$dir/out"
        fi
    done <"$dir/modules"
done

echo "$((total - failures)) of $total blocks ok, $fragments fragments skipped"
[ "$failures" -eq 0 ]

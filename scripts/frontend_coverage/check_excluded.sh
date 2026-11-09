#!/bin/sh
# The guard of the count's key (#2832, #2854): the objects `unrelated_object` in main.smith leaves out of the key hold only
# the code of other verbs. Fails when an object outside that set (and outside smith.o and rt_runtime_verbs.o, which dispatch
# the verbs) calls a symbol one of them defines, or when this list and the one in main.smith differ.
# usage: check_excluded.sh <smith folder> <main.smith>
set -eu
folder=$1
source=$2
cd "$folder"
exact="rt_runtime_test_cache.o rt_runtime_docs.o rt_smith_protocol_dap.o rt_smith_protocol_mcp.o lldb_dap_shim.o lldb_server_shim.o"
excluded=$(for o in $exact rt_runtime_mcp*.o front_lsp_*.o; do [ -f "$o" ] && echo "$o"; done | tr '\n' ' ')
listed=$(sed -n '/static Bool unrelated_object/,/^}/p' "$source" | grep -o '"[a-z_]*\.o"' | tr -d '"' | sort | tr '\n' ' ')
want=$(echo $exact | tr ' ' '\n' | sort | tr '\n' ' ')
[ "$listed" = "$want" ] || { echo "check_excluded: the list here ($want) differs from unrelated_object in $source ($listed)"; exit 1; }
grep -qF 'starts_with(name, "rt_runtime_mcp")' "$source" && grep -qF 'starts_with(name, "front_lsp_")' "$source" ||
    { echo "check_excluded: the prefixes differ from $source"; exit 1; }
defined=$(mktemp); trap 'rm -f "$defined"' EXIT
# shellcheck disable=SC2086
nm -g --defined-only $excluded | awk 'NF == 3 && $2 ~ /^[TDRBSVW]$/ {print $3}' | sort -u >"$defined"
status=0
for o in *.o; do
    case " $excluded " in *" $o "*) continue ;; esac
    case "$o" in smith.o | rt_runtime_verbs.o) continue ;; esac
    hits=$(nm -u "$o" 2>/dev/null | awk 'NF == 2 {print $2}' | sort -u | comm -12 - "$defined" | head -n 5 | tr '\n' ' ')
    if [ -n "$hits" ]; then
        echo "check_excluded: $o calls $hits(defined in an object left out of the count's key)"
        status=1
    fi
done
[ "$status" = 0 ] && echo "check_excluded: $(echo "$excluded" | wc -w) objects, no caller outside the verbs' own code"
exit $status

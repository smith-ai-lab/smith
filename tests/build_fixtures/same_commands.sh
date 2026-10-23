#!/bin/sh
# The compile and link commands `make -n -B` lists and `smith build -t commands <targets>` lists, each with the compiler
# called CC and the dependency flags out, as the shell reads them, one per line and sorted, in make.cmds and smith.cmds
# (tests/make_test.smith compares them). Usage: same_commands.sh target...
canonical() {
    sed -E -e 's#^([^ ]*/)?(smith-cc|cc|clang|gcc) #CC #' -e 's#^([^ ]*/)?(smith-c\+\+|c\+\+|g\+\+|clang\+\+) #CXX #' \
        -e 's# -MMD -MF [^ ]*\.d # #' -e 's# -MMD # #' -e 's# -MP # #' -e 's#  *# #g' |
        grep '^CC \|^CXX ' |
        while IFS= read -r line; do
            eval "set -- $line" 2>/dev/null || {
                echo "$line"
                continue
            }
            printf '%s' "$1"
            shift
            for word in "$@"; do printf ' %s' "$word"; done
            echo
        done | sort -u
}
make -n -B 2>/dev/null | canonical >make.cmds
smith build -t commands "$@" 2>/dev/null >smith.raw
while IFS= read -r line; do
    case $line in
    "sh .smith/command/"*) cat "${line#sh }" ;;
    *) echo "$line" ;;
    esac
done <smith.raw | canonical >smith.cmds

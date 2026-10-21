#!/bin/sh
# Builds LLVM 23.1.3 with RTTI and exceptions on, for the prover: `smith build llvm-prove`. Alive2 needs both (its README).
# It is scripts/llvm.sh, whatever that script does today, with three things changed on the way through: the folder
# ($CACHE/smith/llvm/23.1.3-prove/{build,install}, beside the install of scripts/llvm.sh, which stays as it is), the two
# CMake options, and the place defer_init.c is found. Arguments are llvm.sh's. scripts/prove-libs.sh then builds Z3 and
# Alive2 against the result.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
script=$(mktemp "${TMPDIR:-/tmp}/llvm-prove.XXXXXX")
trap 'rm -f "$script"' EXIT INT TERM
sed -e 's#^cache=\(.*\)/smith/llvm/23.1.3$#cache=\1/smith/llvm/23.1.3-prove#' \
    -e 's#-DLLVM_ENABLE_ASSERTIONS=OFF \\#-DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_EH=ON \\#' \
    -e "s#\"\$(dirname \"\$0\")/#\"$here/#" "$here/llvm.sh" >"$script"
grep -q 'LLVM_ENABLE_RTTI=ON' "$script" && grep -q '23.1.3-prove' "$script" || {
    echo "llvm-prove.sh: scripts/llvm.sh changed in a way this script does not know" >&2
    exit 1
}
sh "$script" "$@"

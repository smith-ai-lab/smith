#!/bin/sh
# The lint identity of smith (#2236, runtime/lint_identity.c): what `smith lint` keys its cache on instead of the binary.
# build.c runs it as `sh scripts/lint_identity.sh <pack name and identity> <c++ compiler> <its flags>...` and compiles the
# hash it prints into smith. The compiler's `-MM` lists the project files shim/tidy.cpp includes (the sources of smith's
# own checks; system headers, LLVM's among them, are left out, and the pack is in the first argument): the list is the
# preprocessor's, never written by hand, so a check that starts to use runtime code is in it from then on. The hash is
# of the pack, then each file's name and SHA-256, sorted.
set -e
pack=$1
compiler=$2
shift 2
deps=$("$compiler" "$@" -MM shim/tidy.cpp) # a compiler that fails stops the script: no hash of the pack alone
files=$(printf '%s\n' "$deps" | tr -d '\\' | tr ' ' '\n' | grep -v ':$' | grep . | sort -u)
case $files in *shim/tidy.cpp*) ;; *) echo "lint identity: shim/tidy.cpp is not in its dependencies" >&2; exit 1 ;; esac
{
    printf 'smith-lint-identity 1\n%s\n' "$pack"
    for file in $files; do
        printf '%s ' "$file"
        shasum -a 256 <"$file"
    done
} | shasum -a 256 | cut -c1-64

#!/bin/sh
# Builds Bitwuzla (the SMT solver `smith prove` asks first; Z3 only finds the model of a counterexample) static, beside Z3
# and Alive2's libraries: $CACHE/smith/llvm/23.1.3-prove/prove/{lib,include}. build.c passes the packages (pinned by SHA-256
# there) and the stamp it writes when this succeeds. Bitwuzla builds with Meson, which fetches its own CaDiCaL and symfpu by
# the hashes and revisions its wrap files pin, so this step needs the network once. It needs GMP and MPFR (brew install gmp
# mpfr), which smith links statically from Homebrew's folder.
set -eu
bitwuzla=$1 meson=$2 python=$3 stamp=$4
cache=${XDG_CACHE_HOME:-$HOME/.cache}/smith/llvm/23.1.3-prove
out=$cache/prove
export SDKROOT=$(xcrun --show-sdk-path)
mkdir -p "$out" "$(dirname "$stamp")"
# Meson writes its subprojects into the source tree, which a package must not change: build from a copy.
rm -rf "$cache/bitwuzla-src"
cp -R "$bitwuzla" "$cache/bitwuzla-src"
chmod -R u+w "$cache/bitwuzla-src"
"$python/bin/python3" "$meson/meson.py" setup "$cache/bitwuzla-build" "$cache/bitwuzla-src" \
    --buildtype=release --default-library=static --prefix="$out" \
    -Dtesting=disabled -Dunit_testing=disabled -Dpython=false -Ddocs=false >/dev/null 2>&1 || true
# Only the libraries: the solver's own program links GMP and MPFR as dynamic libraries, which smith does not.
ninja -C "$cache/bitwuzla-build" subprojects/cadical-rel-2.1.2/src/libcadical.a src/libbzlautil.a src/lib/libbzlarng.a \
    src/lib/libbitwuzlabv.a src/lib/libbitwuzlabb.a src/lib/libbitwuzlals.a src/libbitwuzla.a >/dev/null
for library in subprojects/cadical-rel-2.1.2/src/libcadical.a src/libbzlautil.a src/lib/libbzlarng.a \
    src/lib/libbitwuzlabv.a src/lib/libbitwuzlabb.a src/lib/libbitwuzlals.a src/libbitwuzla.a; do
    cp "$cache/bitwuzla-build/$library" "$out/lib/"
done
mkdir -p "$out/include/bitwuzla"
cp -R "$cache/bitwuzla-src/include/bitwuzla/." "$out/include/bitwuzla/"
echo "$out" >"$stamp"

#!/bin/sh
# Builds Bitwuzla (the SMT solver `smith prove` asks first; Z3 only finds the model of a counterexample) and what it needs,
# all static, beside Z3 and Alive2's libraries: $CACHE/smith/llvm/<name>/prove/{lib,include}. Usage: bitwuzla.sh <bitwuzla>
# <meson> <python> <ninja> <gmp> <mpfr> <cadical> <symfpu>, the packages pinned by SHA-256 in build.c and the ninja of
# scripts/ninja.sh. Nothing comes from Homebrew, and nothing from the network:
#   - GMP and MPFR are built with their own configure and the system's make, into $out;
#   - Meson would fetch CaDiCaL and symfpu by the hashes and revisions its wrap files pin; the same two archives are
#     packages here (CaDiCaL by the wrap's own hash, symfpu's by the revision's archive) and go where the wraps' downloads
#     would, with the wraps' overlays (subprojects/packagefiles);
#   - Bitwuzla's Meson looks GMP and MPFR up with pkg-config, which is not part of macOS: in the copy of the source it is
#     told where the static libraries are instead.
set -eu
bitwuzla=$1 meson=$2 python=$3 ninja=$4 gmp=$5 mpfr=$6 cadical=$7 symfpu=$8
cache=${XDG_CACHE_HOME:-$HOME/.cache}/smith/llvm/${SMITH_LLVM_NAME:-23.1.3-r2}
out=$cache/prove
export SDKROOT=$(xcrun --show-sdk-path)
mkdir -p "$out/lib" "$out/include"
jobs=$(sysctl -n hw.ncpu)
# One tool path for every build below: macOS's own tools, our ninja and python.
export PATH="$(dirname "$ninja"):$python/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export CC=/usr/bin/clang CXX=/usr/bin/clang++

# GMP and MPFR, in copies of their sources (a package is never written to). MPFR is built against the GMP just made.
if [ ! -f "$out/lib/libmpfr.a" ]; then
    for library in gmp mpfr; do
        eval "package=\$$library"
        rm -rf "$cache/$library-build"
        cp -R "$package" "$cache/$library-build"
        chmod -R u+w "$cache/$library-build"
    done
    (cd "$cache/gmp-build" && ./configure --prefix="$out" --disable-shared --enable-static --enable-cxx &&
        make -j"$jobs" && make install) >"$cache/gmp-build.log" 2>&1 || { tail -30 "$cache/gmp-build.log" >&2; exit 1; }
    (cd "$cache/mpfr-build" && ./configure --prefix="$out" --with-gmp="$out" --disable-shared --enable-static &&
        make -j"$jobs" && make install) >"$cache/mpfr-build.log" 2>&1 || { tail -30 "$cache/mpfr-build.log" >&2; exit 1; }
fi

# Meson writes its subprojects into the source tree: build from a copy, with the two subprojects in place.
rm -rf "$cache/bitwuzla-src" "$cache/bitwuzla-build"
cp -R "$bitwuzla" "$cache/bitwuzla-src"
chmod -R u+w "$cache/bitwuzla-src"
sub=$cache/bitwuzla-src/subprojects
cp -R "$cadical" "$sub/cadical-rel-2.1.2"
cp -R "$sub/packagefiles/cadical/." "$sub/cadical-rel-2.1.2/"
cp -R "$symfpu" "$sub/symfpu"
cp -R "$sub/packagefiles/symfpu/." "$sub/symfpu/"
chmod -R u+w "$sub"
# GMP and MPFR by find_library, in the one place Bitwuzla looks them up.
LIBDIR=$out/lib perl -0pi -e 's|gmp_dep = dependency\(.gmp.,.*?static: build_static\)\nmpfr_dep = dependency\(.mpfr.,.*?static: build_static\)|gmp_dep = cpp_compiler.find_library(\x27gmp\x27, dirs: [\x27$ENV{LIBDIR}\x27], static: true)\nmpfr_dep = cpp_compiler.find_library(\x27mpfr\x27, dirs: [\x27$ENV{LIBDIR}\x27], static: true)|s' \
    "$cache/bitwuzla-src/src/meson.build"
grep -q "find_library('gmp'" "$cache/bitwuzla-src/src/meson.build" || {
    echo "bitwuzla.sh: Bitwuzla's src/meson.build changed in a way this script does not know" >&2
    exit 1
}
"$python/bin/python3" -I "$meson/meson.py" setup "$cache/bitwuzla-build" "$cache/bitwuzla-src" \
    --buildtype=release --default-library=static --prefix="$out" --wrap-mode=nodownload \
    -Dc_args="-I$out/include" -Dcpp_args="-I$out/include" \
    -Dtesting=disabled -Dunit_testing=disabled -Dpython=false -Ddocs=false >"$cache/bitwuzla-setup.log" 2>&1 || {
    tail -30 "$cache/bitwuzla-setup.log" >&2
    exit 1
}
# Only the libraries: the solver's own program is not wanted.
libs="subprojects/cadical-rel-2.1.2/src/libcadical.a src/libbzlautil.a src/lib/libbzlarng.a src/lib/libbitwuzlabv.a
    src/lib/libbitwuzlabb.a src/lib/libbitwuzlals.a src/libbitwuzla.a"
# shellcheck disable=SC2086
nice "$ninja" -C "$cache/bitwuzla-build" $libs >"$cache/bitwuzla-build.log" 2>&1 || { tail -40 "$cache/bitwuzla-build.log" >&2; exit 1; }
for library in $libs; do
    cp "$cache/bitwuzla-build/$library" "$out/lib/"
done
mkdir -p "$out/include/bitwuzla"
cp -R "$cache/bitwuzla-src/include/bitwuzla/." "$out/include/bitwuzla/"

#!/bin/sh
# Builds Enzyme (reverse and forward mode automatic differentiation of LLVM IR), static, against the LLVM install of
# scripts/llvm.sh: libEnzymeStatic-23.a, into $CACHE/smith/enzyme/<version>-<llvm configuration>/lib. It is its own
# folder, keyed by both, so building it never touches the LLVM install that other smiths link. build.c passes the package
# (pinned by SHA-256 there). Run again with nothing changed, cmake and ninja find nothing to do.
#   -DENZYME_RUNPASS makes the static library hook the optimization pipelines (without it nothing runs Enzyme);
#   ENZYME_CLANG=OFF: no clang plugin, smith runs Enzyme at link time (shim/lld.cpp);
#   patches/enzyme/*.patch go into a copy of the package, as the LLVM patches do.
set -eu
src=$1 cmake=$2 llvm=$3 ninja=$4 out=$5
name=$(basename "$(dirname "$llvm")")
work=$(dirname "$out")/build-$(basename "$out")
patches=$(cd "$(dirname "$0")/.." && pwd)/patches/enzyme
cmake=$cmake/CMake.app/Contents/bin/cmake
sdk=$(xcrun --show-sdk-path)
host=/usr/bin
mkdir -p "$out/lib" "$work"
if [ ! -f "$work/.patched" ]; then
    rm -rf "$work/src"
    mkdir -p "$work/src"
    cp -R "$src/." "$work/src/"
    chmod -R u+w "$work/src"
    for patch in "$patches"/*.patch; do patch -p1 -s -d "$work/src" <"$patch"; done
    : >"$work/.patched"
fi
SDKROOT=$sdk "$cmake" -G Ninja -S "$work/src/enzyme" -B "$work/build" \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local" \
    -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_C_COMPILER=$host/clang -DCMAKE_CXX_COMPILER=$host/clang++ \
    -DCMAKE_OSX_SYSROOT="$sdk" -DLLVM_DIR="$llvm/lib/cmake/llvm" -DClang_DIR="$llvm/lib/cmake/clang" \
    -DENZYME_STATIC_LIB=ON -DENZYME_CLANG=OFF -DLLVM_EXTERNAL_LIT=/nonexistent "-DCMAKE_CXX_FLAGS=-DENZYME_RUNPASS"
SDKROOT=$sdk nice "$ninja" -C "$work/build" EnzymeStatic-23
cp "$work/build/Enzyme/libEnzymeStatic-23.a" "$out/lib/libEnzymeStatic-23.a.tmp"
mv "$out/lib/libEnzymeStatic-23.a.tmp" "$out/lib/libEnzymeStatic-23.a"
echo "enzyme-0.0.303 llvm-$name" >"$out/lib/smith-enzyme.id"

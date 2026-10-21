#!/bin/sh
# Builds LLVM 23.1.3 with RTTI and exceptions on, static, from source, for the prover: `smith build llvm-prove`. Alive2 needs
# both (its README). Otherwise the options of scripts/llvm.sh, minus clang-tools-extra and lld, which the prover does not
# link; it goes to $CACHE/smith/llvm/23.1.3-prove/{build,install}, beside the install of scripts/llvm.sh, which stays as it
# is. scripts/prove-libs.sh then builds Z3 and Alive2 against it.
set -eu
src=$1 cmake=$2 python=$3 stamp=$4
cache=${XDG_CACHE_HOME:-$HOME/.cache}/smith/llvm/23.1.3-prove
mkdir -p "$cache" "$(dirname "$stamp")"
# The host compiler that builds LLVM is Apple's; the clang it makes finds the SDK at run time.
sdk=$(xcrun --show-sdk-path)
host=/usr/bin
# Release, no assertions, static libraries only. Off: everything that would link a dylib from outside /usr/lib and
# /System (zlib, zstd, libxml2, z3, libedit, terminfo, ffi, curl). Off for build time only: tests, benchmarks, examples,
# docs, bindings, and the compiler-rt parts smith does not use (libfuzzer, memprof, xray, orc, ctx_profile).
"$cmake/CMake.app/Contents/bin/cmake" -G Ninja -S "$src/llvm" -B "$cache/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$cache/install" \
    -DCMAKE_MAKE_PROGRAM="$(command -v ninja)" \
    -DCMAKE_C_COMPILER=$host/clang -DCMAKE_CXX_COMPILER=$host/clang++ \
    -DCMAKE_OSX_SYSROOT="$sdk" \
    -DPython3_EXECUTABLE="$python/bin/python3" \
    -DLLVM_ENABLE_PROJECTS='clang' \
    -DLLVM_ENABLE_RUNTIMES='compiler-rt' \
    -DLLVM_TARGETS_TO_BUILD=AArch64 \
    -DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_EH=ON \
    -DBUILD_SHARED_LIBS=OFF -DLLVM_BUILD_LLVM_DYLIB=OFF -DLLVM_LINK_LLVM_DYLIB=OFF -DCLANG_LINK_CLANG_DYLIB=OFF \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_Z3_SOLVER=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_FFI=OFF -DLLVM_ENABLE_CURL=OFF \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_ENABLE_BINDINGS=OFF -DCLANG_INCLUDE_TESTS=OFF \
    -DCOMPILER_RT_INCLUDE_TESTS=OFF -DCOMPILER_RT_BUILD_LIBFUZZER=OFF -DCOMPILER_RT_BUILD_MEMPROF=OFF \
    -DCOMPILER_RT_BUILD_XRAY=OFF -DCOMPILER_RT_BUILD_ORC=OFF -DCOMPILER_RT_BUILD_CTX_PROFILE=OFF
nice ninja -C "$cache/build" install
# The clang binary's own main is a library function, clang_main, in these four objects (the fifth, clang-driver.cpp.o, is
# its main). smith links them to compile in its own process, so they go beside the libraries as libclangmain.a.
objects=$cache/build/tools/clang/tools/driver/CMakeFiles/clang.dir
rm -f "$cache/install/lib/libclangmain.a"
/usr/bin/ar rcs "$cache/install/lib/libclangmain.a" "$objects/driver.cpp.o" "$objects/cc1_main.cpp.o" \
    "$objects/cc1as_main.cpp.o" "$objects/cc1gen_reproducer_main.cpp.o"
# What the compiler smith links is: the version, and a hash of the archives it links and of the four objects in libclangmain.a (not the archive, which holds the time
# it was made).
# `smith build` compiles it into smith, which reports it as the identity of its clang, so a smith rebuilt against the same
# LLVM leaves every project's objects alone and a rebuilt LLVM rebuilds them.
hash=$(cat "$cache"/install/lib/libclang[A-Z]*.a "$cache"/install/lib/libLLVM*.a \
    "$objects/driver.cpp.o" "$objects/cc1_main.cpp.o" "$objects/cc1as_main.cpp.o" "$objects/cc1gen_reproducer_main.cpp.o" |
    /usr/bin/shasum -a 256 | cut -d' ' -f1)
echo "llvm-23.1.3 $hash" >"$cache/install/lib/smith-clang.id"
echo "$cache/install" >"$stamp"

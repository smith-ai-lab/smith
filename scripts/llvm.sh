#!/bin/sh
# Builds smith's pinned LLVM, static, from source: `smith build llvm`. build.c passes the three packages (all pinned by
# SHA-256 there) and the stamp it writes when this succeeds. Run again with nothing changed, cmake and ninja find
# nothing to do. The work lives in the package cache, not in build/, because `smith build clean` would delete an
# hour's build: $CACHE/smith/llvm/<version>/{build,install}.
set -eu
src=$1 cmake=$2 python=$3 stamp=$4 ninja=$5
# One configuration serves the compiler, the prover and `.smith` (#20): RTTI and exceptions on (Alive2 needs them), and the
# token hook (#11). A change to either is a new folder: r2 is the second configuration of 23.1.3. SMITH_LLVM_NAME builds
# into another folder (an experiment next to the shared install).
cache=${XDG_CACHE_HOME:-$HOME/.cache}/smith/llvm/${SMITH_LLVM_NAME:-23.1.3-r2}
mkdir -p "$cache" "$(dirname "$stamp")"
# --- step: the token hook (patches/clang-token-hook.patch; issue #11) ---------------------------------------------------
# A token transformer on the Preprocessor, for .smith files; off unless smith installs it. The package is read-only to
# everyone else (other installs are built from it), so the patch goes into a copy, once: a copy that has it is left alone.
# Remove this step, and the file, to build plain LLVM.
patch=$(cd "$(dirname "$0")/.." && pwd)/patches/clang-token-hook.patch
if [ ! -f "$cache/src/.hooked" ]; then
    rm -rf "$cache/src"
    mkdir -p "$cache/src"
    cp -R "$src/." "$cache/src/"
    chmod -R u+w "$cache/src"
    patch -p1 -s -d "$cache/src" <"$patch"
    : >"$cache/src/.hooked"
fi
src=$cache/src
# --- end of the token hook step -----------------------------------------------------------------------------------------
# The host compiler that builds LLVM is Apple's; the clang it makes finds the SDK at run time.
sdk=$(xcrun --show-sdk-path)
host=/usr/bin
# Release, no assertions, static libraries only. Off: everything that would link a dylib from outside /usr/lib and
# /System (zlib, zstd, libxml2, z3, libedit, terminfo, ffi, curl). Off for build time only: tests, benchmarks, examples,
# docs, bindings, and the compiler-rt parts smith does not use (libfuzzer, memprof, xray, orc, ctx_profile).
"$cmake/CMake.app/Contents/bin/cmake" -G Ninja -S "$src/llvm" -B "$cache/build" \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local" \
    -DCMAKE_INSTALL_PREFIX="$cache/install" \
    -DCMAKE_MAKE_PROGRAM="$ninja" \
    -DCMAKE_C_COMPILER=$host/clang -DCMAKE_CXX_COMPILER=$host/clang++ \
    -DCMAKE_OSX_SYSROOT="$sdk" \
    -DPython3_EXECUTABLE="$python/bin/python3" \
    -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra;lld;lldb' \
    -DLLVM_ENABLE_RUNTIMES='compiler-rt' \
    -DLLVM_TARGETS_TO_BUILD=AArch64 \
    -DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_EH=ON \
    -DBUILD_SHARED_LIBS=OFF -DLLVM_BUILD_LLVM_DYLIB=OFF -DLLVM_LINK_LLVM_DYLIB=OFF -DCLANG_LINK_CLANG_DYLIB=OFF \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_Z3_SOLVER=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_FFI=OFF -DLLVM_ENABLE_CURL=OFF \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_ENABLE_BINDINGS=OFF -DCLANG_INCLUDE_TESTS=OFF \
    -DLLDB_USE_SYSTEM_DEBUGSERVER=ON -DLLDB_ENABLE_PYTHON=OFF -DLLDB_ENABLE_LUA=OFF -DLLDB_ENABLE_LIBEDIT=OFF \
    -DLLDB_ENABLE_CURSES=OFF -DLLDB_ENABLE_LZMA=OFF -DLLDB_ENABLE_LIBXML2=OFF -DLLDB_INCLUDE_TESTS=OFF \
    -DCOMPILER_RT_INCLUDE_TESTS=OFF -DCOMPILER_RT_BUILD_LIBFUZZER=OFF -DCOMPILER_RT_BUILD_MEMPROF=OFF \
    -DCOMPILER_RT_BUILD_XRAY=OFF -DCOMPILER_RT_BUILD_ORC=OFF -DCOMPILER_RT_BUILD_CTX_PROFILE=OFF
nice "$ninja" -C "$cache/build" install
# The clang binary's own main is a library function, clang_main, in these four objects (the fifth, clang-driver.cpp.o, is
# its main). smith links them to compile in its own process, so they go beside the libraries as libclangmain.a.
objects=$cache/build/tools/clang/tools/driver/CMakeFiles/clang.dir
rm -f "$cache/install/lib/libclangmain.a"
/usr/bin/ar rcs "$cache/install/lib/libclangmain.a" "$objects/driver.cpp.o" "$objects/cc1_main.cpp.o" \
    "$objects/cc1as_main.cpp.o" "$objects/cc1gen_reproducer_main.cpp.o"
# ---- step: the tools (#3). Self-contained: it reads the build tree and the install, and writes only $tools and
# ---- libsmithtools.a. Other steps of this script (the identity at its end included) do not depend on how it works.
# clang-format, clang-tidy and clangd are linked into smith as `smith fmt`, `smith lint` and `smith lsp` (shim/tool.h says
# why each of the following is done). libsmithtools.a holds patched copies of the objects, beside the pristine libraries,
# and is linked first, so its members are found before the originals':
#   - clang-format's main is its one object, ClangFormat.cpp.o, with the symbol `main` renamed. clang-tidy's and clangd's
#     are functions in libclangTidyMain.a and libclangdMain.a, whose members are copied as they are;
#   - those objects' static initializers, which declare the flags and collide (clang-format and clang-tidy both have
#     -fallback-style), are moved out of __mod_init_func (scripts/defer_init.c, built here with the host compiler), to run when the
#     tool runs;
#   - clangd's code actions ("tweaks") are objects that register themselves by static initializers and that nothing
#     names, so they are copied too, with their initializers deferred like clangd's own, and the whole of
#     libsmithtools.a is linked (-force_load) so that none is left out;
#   - the one call in clangd (CompileCommands.cpp.o) and the one in clang-tidy (Tooling.cpp.o) that find the resource
#     directory by the path of the executable are redirected to smith_resource_path (shim/tool.h).
tools=$cache/build/smith-tools
rm -rf "$tools"
mkdir -p "$tools/format" "$tools/tidy" "$tools/clangd" "$tools/daemon" "$tools/tooling" "$tools/tweaks" "$tools/out"
objcopy=$cache/install/bin/llvm-objcopy
"$objcopy" --redefine-sym _main=_smith_clang_format_main \
    "$cache/build/tools/clang/tools/clang-format/CMakeFiles/clang-format.dir/ClangFormat.cpp.o" "$tools/format/ClangFormat.o"
(cd "$tools/tidy" && /usr/bin/ar x "$cache/install/lib/libclangTidyMain.a")
(cd "$tools/clangd" && /usr/bin/ar x "$cache/install/lib/libclangdMain.a")
(cd "$tools/daemon" && /usr/bin/ar x "$cache/install/lib/libclangDaemon.a" CompileCommands.cpp.o)
(cd "$tools/tooling" && /usr/bin/ar x "$cache/install/lib/libclangTooling.a" Tooling.cpp.o)
(cd "$tools/tweaks" && /usr/bin/ar x "$cache/install/lib/libclangDaemonTweaks.a")
defer=$tools/defer_init
$host/clang -std=gnu23 -O1 -Wall -Wextra -Werror "$(dirname "$0")/defer_init.c" -o "$defer"
"$defer" "$tools/format/ClangFormat.o" "$tools/out/ClangFormat.o" fmt
"$defer" "$tools/tidy/ClangTidyMain.cpp.o" "$tools/out/ClangTidyMain.o" tidy
"$defer" "$tools/clangd/ClangdMain.cpp.o" "$tools/out/ClangdMain.o" lsp
"$defer" "$tools/clangd/Check.cpp.o" "$tools/out/Check.o" lsp
for tweak in "$tools"/tweaks/*.o; do
    "$defer" "$tweak" "$tools/out/tweak_$(basename "$tweak")" lsp >/dev/null
done
# clang::GetResourcesPath(char const *, void *) to smith_resource_path(char const *, void *)
get_resources=__ZN5clang16GetResourcesPathEPKcPv
smith_resources=__Z19smith_resource_pathPKcPv
"$objcopy" --redefine-sym "$get_resources=$smith_resources" "$tools/daemon/CompileCommands.cpp.o" "$tools/out/CompileCommands.o"
"$objcopy" --redefine-sym "$get_resources=$smith_resources" "$tools/tooling/Tooling.cpp.o" "$tools/out/Tooling.o"
rm -f "$cache/install/lib/libsmithtools.a"
/usr/bin/ar rcs "$cache/install/lib/libsmithtools.a" "$tools"/out/*.o
# ---- end of the tools step.

# ---- step: lldb (#20, for `smith dbg` and `smith dap`, #7). Self-contained: it reads the build tree and the install, and
# ---- writes only the lldb archives in the install. lldb is built without Python, libedit, curses, lua, lzma and libxml2,
# ---- and with the system's debugserver (LLDB_USE_SYSTEM_DEBUGSERVER). Its components are static archives (build/lib), which
# ---- the build folds into liblldb.dylib; they are copied to the install beside the others, as liblldb<Component>.a, and the
# ---- mains of the lldb driver and of lldb-dap (their one `main` renamed) are kept as libsmithlldb.a, the way clang's are.
lldbdir=$cache/build/tools/lldb/tools
mkdir -p "$cache/build/smith-lldb"
rm -f "$cache/install/lib/libsmithlldb.a" "$cache/build/smith-lldb"/*.o
cp "$cache"/build/lib/liblldb[A-Z]*.a "$cache/install/lib/"
"$cache/install/bin/llvm-objcopy" --redefine-sym _main=_smith_lldb_main "$lldbdir/driver/CMakeFiles/lldb.dir/Driver.cpp.o" \
    "$cache/build/smith-lldb/Driver.o"
cp "$lldbdir/driver/CMakeFiles/lldb.dir/Platform.cpp.o" "$cache/build/smith-lldb/Platform.o"
"$cache/install/bin/llvm-objcopy" --redefine-sym _main=_smith_lldb_dap_main \
    "$lldbdir/lldb-dap/tool/CMakeFiles/lldb-dap.dir/lldb-dap.cpp.o" "$cache/build/smith-lldb/lldb-dap.o"
/usr/bin/ar rcs "$cache/install/lib/libsmithlldb.a" "$cache"/build/smith-lldb/*.o
# ---- end of the lldb step.

# ---- step: the identity, written last. Every step above and any added later goes before it, and what it hashes is
# ---- everything smith links.
# What the compiler and the linker smith links are: the version, and a hash of the archives it links (clang's, LLVM's and
# lld's) and of the four objects in libclangmain.a (not the archive, which holds the time it was made).
# `smith build` compiles it into smith, which reports it as the identity of its clang, so a smith rebuilt against the same
# LLVM leaves every project's objects alone and a rebuilt LLVM rebuilds them. lld is in it because the program the driver
# runs as `ld` is smith, and a smith that links with lld must not share clang's folder, with its `bin/ld`, with one that
# does not.
hash=$(cat "$cache"/install/lib/libclang[A-Z]*.a "$cache"/install/lib/libLLVM*.a "$cache"/install/lib/liblldMachO.a \
    "$cache"/install/lib/liblldCommon.a \
    "$cache"/install/lib/libclangd[A-Z]*.a \
    "$objects/driver.cpp.o" "$objects/cc1_main.cpp.o" "$objects/cc1as_main.cpp.o" "$objects/cc1gen_reproducer_main.cpp.o" \
    "$tools"/out/*.o "$cache"/install/lib/liblldb[A-Z]*.a "$cache"/build/smith-lldb/*.o |
    /usr/bin/shasum -a 256 | cut -d' ' -f1)
echo "llvm-23.1.3 $hash" >"$cache/install/lib/smith-clang.id"
echo "$cache/install" >"$stamp"

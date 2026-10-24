#!/bin/sh
# Builds smith's pinned LLVM, static, from source: `smith build llvm`. build.c passes the three packages (all pinned by
# SHA-256 there) and the stamp it writes when this succeeds. Run again with nothing changed, cmake and ninja find
# nothing to do. The work lives in the package cache, not in build/, because `smith build clean` would delete an
# hour's build: $CACHE/smith/llvm/<version>/{build,install}.
set -eu
src=$1 cmake=$2 python=$3 stamp=$4 ninja=$5
# One configuration serves the compiler, the prover and `.smith` (#20): RTTI and exceptions on (Alive2 needs them), and the
# token hook (#11). A change to either, or to a patch, is a new folder: r8 is the eighth configuration of 23.1.3 (r3 added
# the editor's defaults, the `.smith` source extension of clang-format's main include, and the command hook; r4 lets the editor run smith-effects; r5 adds the constant evaluator for C function bodies in `.smith` files and clang-format's tuple and slice forms; r6 adds compile-time failure annotations, the evaluator under -fmodules and constexpr pointers into literals; r7 folds a call of a C function in a constant context without -Wgnu-folding-constant, and clang-format keeps the space of `[const :]`; r8 formats payload enums like structs and spaces a variant in the dot rule). SMITH_LLVM_NAME builds
# into another folder (an experiment next to the shared install).
cache=${XDG_CACHE_HOME:-$HOME/.cache}/smith/llvm/${SMITH_LLVM_NAME:-23.1.3-r8}
mkdir -p "$cache" "$(dirname "$stamp")"
# --- step: the patches (patches/*.patch): the token hook (issue #11) and clang-format for .smith (issue #25) -------------
# patches/clang-token-hook.patch is a token transformer on the Preprocessor, for .smith files; off unless smith installs
# it. patches/clang-format-smith.patch teaches clang-format that a .smith file is C with `::` paths and module/import
# lines. The package is read-only to everyone else (other installs are built from it), so the patches go into a copy,
# once. The copy keeps the patches it has in .patches/, one file each: a patch that changed (or is gone) is taken out
# with the ones applied after it, and the new revisions and new patches are put in, so only the files they touch change
# and the rebuild is incremental. A patch must apply to the sources as the patches before it left them, and to them
# alone. A .hooked file (one patch, from before there were two) is the hook patch already applied; an empty one means a
# fresh copy. Remove this step, and the patches, to build plain LLVM.
patches=$(cd "$(dirname "$0")/.." && pwd)/patches
applied=$cache/src/.patches
if [ -s "$cache/src/.hooked" ]; then
    mkdir -p "$applied"
    mv "$cache/src/.hooked" "$applied/clang-token-hook.patch"
elif [ ! -d "$applied" ]; then
    rm -rf "$cache/src"
    mkdir -p "$cache/src"
    cp -R "$src/." "$cache/src/"
    chmod -R u+w "$cache/src"
    mkdir -p "$applied"
fi
# Take out, last first, everything from the first applied patch that differs from patches/ on.
stale=
for name in $(ls "$applied"); do
    if [ -z "$stale" ] && ! cmp -s "$patches/$name" "$applied/$name"; then stale=yes; fi
    if [ -n "$stale" ]; then echo "$name"; fi
done | sort -r >"$cache/src/.unapply"
while read -r name; do
    patch -R -p1 -s -d "$cache/src" <"$applied/$name"
    rm "$applied/$name"
done <"$cache/src/.unapply"
rm -f "$cache/src/.unapply"
for patch in "$patches"/*.patch; do
    name=$(basename "$patch")
    if [ ! -f "$applied/$name" ]; then
        patch -p1 -s -d "$cache/src" <"$patch"
        cp "$patch" "$applied/$name"
    fi
done
src=$cache/src
# --- end of the patches step -----------------------------------------------------------------------------------------
# The host compiler that builds LLVM is Apple's; the clang it makes finds the SDK at run time.
sdk=$(xcrun --show-sdk-path)
host=/usr/bin
# ---- lldb options (#7), kept apart so that they merge with other configurations. Off, with what each costs: Python
# ---- (the `script` command, Python pretty printers and scripted commands: none run), curses (the `gui` command), lzma
# ---- (compressed symbol sections), libxml2 (Apple plist and platform XML reading), Lua, Tree-sitter (source colouring
# ---- by grammar). On: libedit, the system's /usr/lib/libedit.dylib (history and line editing in `smith dbg`), the
# ---- system's debugserver (smith.c finds it and sets LLDB_DEBUGSERVER_PATH), and protocol servers (lldb's MCP, in lldb).
lldb_options='-DLLDB_ENABLE_PYTHON=OFF -DLLDB_ENABLE_LUA=OFF -DLLDB_ENABLE_CURSES=OFF -DLLDB_ENABLE_LZMA=OFF
    -DLLDB_ENABLE_LIBXML2=OFF -DLLDB_ENABLE_TREESITTER=OFF -DLLDB_ENABLE_LIBEDIT=ON -DLLDB_USE_SYSTEM_DEBUGSERVER=ON
    -DLLDB_INCLUDE_TESTS=OFF -DLLDB_BUILD_FRAMEWORK=OFF'
# ---- end of the lldb options.
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
    -DCOMPILER_RT_INCLUDE_TESTS=OFF -DCOMPILER_RT_BUILD_LIBFUZZER=OFF -DCOMPILER_RT_BUILD_MEMPROF=OFF \
    -DCOMPILER_RT_BUILD_XRAY=OFF -DCOMPILER_RT_BUILD_ORC=OFF -DCOMPILER_RT_BUILD_CTX_PROFILE=OFF $lldb_options
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

# ---- step: lldb and lldb-dap (#7). Self-contained: it reads the build tree and the install, and writes only
# ---- install/lib/liblldb[A-Z]*.a and libsmithlldb.a. Only the identity (the hash below) names what it makes.
# `smith dbg` and `smith dap` run lldb's driver and lldb-dap's tool in smith's process. LLVM installs lldb as a dylib and
# no static libraries, so the static libraries of the build tree (lldb's core and plugins) are copied into the install,
# and libsmithlldb.a holds the objects that make liblldb.dylib (the SB API: its sources are in no library), and the
# objects of the two mains, with the symbol `main` renamed (shim/lldb.cpp and shim/lldb_dap.cpp name them). The static
# initializers of all of them are moved out of the section dyld runs at load (scripts/defer_init.c, as for the tools),
# to run when `smith dbg` or `smith dap` does (shim/tool.h): lldb's cost smith 9 ms at the start of every process,
# compiles included, in initializers that only a debugger needs.
lldb_build=$cache/build/tools/lldb
lldb_out=$cache/build/smith-lldb
rm -rf "$lldb_out"
mkdir -p "$lldb_out/objects"
lldb_defer=$lldb_out/defer_init
$host/clang -std=gnu23 -O1 -Wall -Wextra -Werror "$(dirname "$0")/defer_init.c" -o "$lldb_defer"
objcopy=$cache/install/bin/llvm-objcopy
"$objcopy" --redefine-sym _main=_smith_lldb_driver_main \
    "$lldb_build/tools/driver/CMakeFiles/lldb.dir/Driver.cpp.o" "$lldb_out/objects/Driver.o"
cp "$lldb_build/tools/driver/CMakeFiles/lldb.dir/Platform.cpp.o" "$lldb_out/objects/Platform.o"
"$objcopy" --redefine-sym _main=_smith_lldb_dap_tool_main \
    "$lldb_build/tools/lldb-dap/tool/CMakeFiles/lldb-dap.dir/lldb-dap.cpp.o" "$lldb_out/objects/lldb-dap.o"
for object in "$lldb_build"/source/API/CMakeFiles/liblldb.dir/*.o; do
    cp "$object" "$lldb_out/objects/api_$(basename "$object")"
done
mkdir -p "$lldb_out/deferred"
for object in "$lldb_out"/objects/*.o; do
    "$lldb_defer" "$object" "$lldb_out/deferred/$(basename "$object")" lldb >/dev/null
done
rm -f "$cache/install/lib/libsmithlldb.a"
/usr/bin/ar rcs "$cache/install/lib/libsmithlldb.a" "$lldb_out"/deferred/*.o
for archive in "$cache"/build/lib/liblldb[A-Z]*.a; do
    name=$(basename "$archive" .a)
    mkdir -p "$lldb_out/members/$name" "$lldb_out/deferred_members/$name"
    (cd "$lldb_out/members/$name" && /usr/bin/ar x "$archive")
    for object in "$lldb_out/members/$name"/*.o; do
        "$lldb_defer" "$object" "$lldb_out/deferred_members/$name/$(basename "$object")" lldb >/dev/null
    done
    rm -f "$cache/install/lib/$name.a"
    /usr/bin/ar rcs "$cache/install/lib/$name.a" "$lldb_out/deferred_members/$name"/*.o
done
# ---- end of the lldb step.

# ---- step: the identity, written last. Every step above and any added later goes before it, and what it hashes is
# ---- everything smith links.
# What the compiler and the linker smith links are: the version, and a hash of the archives it links (clang's, LLVM's and
# lld's) and of the four objects in libclangmain.a (not the archive, which holds the time it was made).
# `smith build` compiles it into smith, which reports it as the identity of its clang, so a smith rebuilt against the same
# LLVM leaves every project's objects alone and a rebuilt LLVM rebuilds them. lld is in it because the program the driver
# runs as `ld` is smith, and a smith that links with lld must not share clang's folder, with its `bin/ld`, with one that
# does not.
# An archive is hashed by the contents of its members (`llvm-ar p`), never by its bytes: `ninja install` runs ranlib on every
# archive it installs, which rewrites the table of contents and the archive's times, so the bytes change on every run of
# this script while the code does not, and the identity (which every project's objects depend on) would change with them.
hash=$(
    {
    for archive in "$cache"/install/lib/libclang[A-Z]*.a "$cache"/install/lib/libLLVM*.a \
        "$cache"/install/lib/liblldMachO.a "$cache"/install/lib/liblldCommon.a "$cache"/install/lib/libclangd[A-Z]*.a; do
        "$cache/install/bin/llvm-ar" p "$archive"
    done
    cat "$objects/driver.cpp.o" "$objects/cc1_main.cpp.o" "$objects/cc1as_main.cpp.o" "$objects/cc1gen_reproducer_main.cpp.o" \
        "$tools"/out/*.o "$lldb_out"/deferred/*.o "$lldb_out"/deferred_members/*/*.o
    } | /usr/bin/shasum -a 256 | cut -d' ' -f1
)
echo "llvm-23.1.3 $hash" >"$cache/install/lib/smith-clang.id"
echo "$cache/install" >"$stamp"

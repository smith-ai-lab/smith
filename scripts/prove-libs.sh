#!/bin/sh
# Builds what `smith prove` links beside LLVM: Z3 and the libraries of Alive2 that hold LLVM IR's semantics (ir, smt,
# util, tools and llvm_util; not its translation-validation plugin or its executables), both static, into
# $CACHE/smith/llvm/<name>/prove/{lib,include}, beside the install of scripts/llvm.sh. Alive2 is built against that LLVM
# (RTTI and exceptions on), whose install is the fifth argument; the sixth is the ninja scripts/ninja.sh made. build.c
# passes the packages (pinned by SHA-256 there). Run again with nothing changed, cmake and ninja find nothing to do.
set -eu
z3=$1 alive2=$2 cmake=$3 python=$4 llvm=$5 ninja=$6
cache=${XDG_CACHE_HOME:-$HOME/.cache}/smith/llvm/${SMITH_LLVM_NAME:-23.1.3-r9}
out=$cache/prove
cmake=$cmake/CMake.app/Contents/bin/cmake
sdk=$(xcrun --show-sdk-path)
host=/usr/bin
mkdir -p "$out/lib" "$out/include" "$cache/tools"

# Z3: the library only, static.
"$cmake" -G Ninja -S "$z3" -B "$cache/z3-build" \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local" -DCMAKE_INSTALL_PREFIX="$out" \
    -DCMAKE_MAKE_PROGRAM="$ninja" \
    -DCMAKE_C_COMPILER=$host/clang -DCMAKE_CXX_COMPILER=$host/clang++ -DCMAKE_OSX_SYSROOT="$sdk" \
    -DPython3_EXECUTABLE="$python/bin/python3" \
    -DZ3_BUILD_LIBZ3_SHARED=OFF -DZ3_BUILD_EXECUTABLE=OFF -DZ3_BUILD_TEST_EXECUTABLES=OFF \
    -DZ3_INCLUDE_GIT_HASH=OFF -DZ3_INCLUDE_GIT_DESCRIBE=OFF -DZ3_ENABLE_EXAMPLE_TARGETS=OFF \
    -DZ3_BUILD_PYTHON_BINDINGS=OFF -DZ3_BUILD_JAVA_BINDINGS=OFF -DZ3_BUILD_DOTNET_BINDINGS=OFF
nice "$ninja" -C "$cache/z3-build" install

# ---- step: Z3's static initializers. libz3.a has about ten thousand, which dyld would run in every smith process at load
# ---- (the prover is in all of them). They are moved to __DATA,__smith_prove (scripts/defer_init.c, as for the tools in
# ---- scripts/llvm.sh), and shim/prove.cpp runs them when `smith prove` does. The archive is rebuilt from the objects of
# ---- Z3's build, in the order Z3's own archive step used.
"$host/clang" -std=gnu23 -O1 -Wall -Wextra -Werror "$(dirname "$0")/defer_init.c" -o "$cache/tools/defer_init"
rm -rf "$cache/z3-deferred"
mkdir -p "$cache/z3-deferred"
count=0
(cd "$cache/z3-build" && "$ninja" -t commands libz3.a | tail -n 1 | tr ' ' '\n' | grep '\.o$') >"$cache/z3-deferred/objects.txt"
while read -r object; do
    count=$((count + 1))
    "$cache/tools/defer_init" "$cache/z3-build/$object" "$cache/z3-deferred/$(printf '%05d' "$count").o" prove >/dev/null
done <"$cache/z3-deferred/objects.txt"
rm -f "$out/lib/libz3.a"
/usr/bin/ar rcs "$out/lib/libz3.a" "$cache"/z3-deferred/*.o
# ---- end of the Z3 step.

# Alive2 finds its version with git, which a package has none of: a stand-in that names the pin. re2c only builds its
# own `alive` tool, which is not built here.
printf '#!/bin/sh\necho smith-pinned\n' >"$cache/tools/git"
chmod +x "$cache/tools/git"
"$cmake" -G Ninja -S "$alive2" -B "$cache/alive2-build" \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local" \
    -DCMAKE_MAKE_PROGRAM="$ninja" \
    -DCMAKE_C_COMPILER=$host/clang -DCMAKE_CXX_COMPILER=$host/clang++ -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_CXX_FLAGS="-Wno-error -Dsmt=alive2_smt" \
    -DGIT_EXECUTABLE="$cache/tools/git" -DRE2C=/usr/bin/true \
    -DBUILD_LLVM_UTILS=1 -DLLVM_DIR="$llvm/lib/cmake/llvm" \
    -DZ3_INCLUDE_DIR="$out/include" -DZ3_LIBRARIES="$out/lib/libz3.a"
nice "$ninja" -C "$cache/alive2-build" ir smt tools util llvm_util
for library in ir smt tools util llvm_util; do
    cp "$cache/alive2-build/lib$library.a" "$out/lib/libalive2_$library.a"
done
# The headers Alive2's own headers include, by the paths they use (ir/state.h, smt/expr.h, ...), with the generated
# version header.
rm -rf "$out/include/alive2"
mkdir -p "$out/include/alive2"
for folder in ir smt util tools llvm_util; do
    mkdir -p "$out/include/alive2/$folder"
    cp "$alive2/$folder"/*.h "$alive2/$folder"/*.inc "$out/include/alive2/$folder/" 2>/dev/null || true
done
cp "$cache/alive2-build/version_gen.h" "$out/include/alive2/"

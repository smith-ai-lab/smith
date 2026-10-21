#!/bin/sh
# End-to-end tests for the two caches (modules and packages), their one pruning rule and `smith cache`, with real ninja
# and clang. Every case uses a cache of its own through XDG_CACHE_HOME, so the real ~/.cache/smith is never read,
# pruned or cleaned. The clock is faked through file times (touch -t), never by waiting.
# Usage: sh tests/cache.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
export XDG_CACHE_HOME=$work/xdg-cache

failures=0
total=0
out=
err=
rc=0

capture() {
    out=$("$@" 2>"$work/err")
    rc=$?
    err=$(cat "$work/err")
}

expect() { # name, expected, actual
    total=$((total + 1))
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected: $2"
        echo "  actual:   $3"
    fi
}

expect_contains() { # name, text, needle
    total=$((total + 1))
    case $2 in
    *"$3"*) echo "ok    $1" ;;
    *)
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected to contain: $3"
        echo "  actual:              $2"
        ;;
    esac
}

exists() { [ -e "$1" ] && echo yes || echo no; }

old=202001010000 # a time years ago, for touch -t
packages=$XDG_CACHE_HOME/smith/packages
modules=$XDG_CACHE_HOME/smith/modules

# Three packages, fetched into the cache.
mkdir -p "$work/src"
for n in 1 2 3; do
    mkdir -p "$work/src/pkg$n-1.0"
    echo "package $n" >"$work/src/pkg$n-1.0/file.txt"
    (cd "$work/src" && tar -czf "../pkg$n.tar.gz" "pkg$n-1.0")
    eval "url$n=file://$work/pkg$n.tar.gz"
    eval "hex$n=\$(shasum -a 256 \"$work/pkg$n.tar.gz\" | cut -d' ' -f1)"
done

# 1. Empty caches.
capture "$smith" cache
expect "cache: exits 0 with nothing cached" 0 "$rc"
expect_contains "cache: the modules path" "$out" "modules: $modules"
expect_contains "cache: the packages path" "$out" "packages: $packages"
expect_contains "cache: no entries" "$out" "0 entries, 0 B"
expect_contains "cache: the rule is stated" "$out" "unused for 31 days is removed, checked at most once a week"
capture "$smith" cache clean
expect "cache clean: with nothing cached exits 0" 0 "$rc"
capture "$smith" cache bogus
expect "cache: an unknown word is a usage error" 2 "$rc"
expect_contains "cache: and the usage is shown" "$err" "usage: smith cache"
capture "$smith" cache clean everything
expect "cache clean: an unknown cache is a usage error" 2 "$rc"
capture "$smith" cache clean modules extra
expect "cache clean: too many arguments are a usage error" 2 "$rc"
capture "$smith" cache -v extra
expect "cache: -v takes nothing else" 2 "$rc"

# 2. Listing packages.
for n in 1 2 3; do
    eval "capture \"$smith\" fetch \"\$url$n\""
done
expect "fetch: each package has a stamp beside it" "yes yes yes" "$(exists "$packages/$hex1.used") $(exists "$packages/$hex2.used") $(exists "$packages/$hex3.used")"
capture "$smith" cache
expect_contains "cache: three packages" "$out" "3 entries"
capture "$smith" cache -v
expect_contains "cache -v: lists the first package" "$out" "$(echo "$hex1" | cut -c1-16)"
expect_contains "cache -v: and its last use" "$out" "last used $(date -u +%Y-%m-%d)"
touch -t 202001020304 "$packages/$hex1.used"
capture "$smith" cache -v
expect_contains "cache -v: the last use is the stamp's time" "$out" "last used 2020-01-02"

# 3. Pruning: a build that uses a package keeps it; the weekly check removes the packages unused for 31 days.
mkdir "$work/p"
cd "$work/p" || exit 1
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *dir = smith_package(b, &(SmithPackage){
                                           .url = smith_option_value(b, "URL", nullptr, "the package"),
                                           .hash = smith_option_value(b, "HASH", nullptr, "its hash"),
                                       });
    smith_write(b, "path.txt", dir);
    smith_default(b, smith_list(b, nullptr));
}
C
use() { capture "$smith" build "-DURL=$1" "-DHASH=sha256:$2" -DOFFLINE; }
touch -t $old "$packages/$hex1.used" "$packages/$hex2.used"
touch "$packages/prune.timestamp"
use "$url3" "$hex3"
expect "prune: a build using a package succeeds" 0 "$rc"
expect "prune: the weekly check is not due, so nothing is removed" "yes yes yes" "$(exists "$packages/$hex1") $(exists "$packages/$hex2") $(exists "$packages/$hex3")"
touch -t $old "$packages/prune.timestamp"
use "$url1" "$hex1" # package 1 is stale, but this build uses it
expect "prune: a build using a stale package keeps it" "0 yes" "$rc $(exists "$packages/$hex1")"
expect "prune: and refreshes its stamp" "$(date -u +%Y)" "$(stat -f %Sm -t %Y "$packages/$hex1.used")"
expect "prune: the unused stale package is removed" "no" "$(exists "$packages/$hex2")"
expect "prune: with its stamp" "no" "$(exists "$packages/$hex2.used")"
expect "prune: a package used lately stays" "yes" "$(exists "$packages/$hex3")"
expect "prune: the check is recorded" "$(date -u +%Y)" "$(stat -f %Sm -t %Y "$packages/prune.timestamp")"
use "$url2" "$hex2"
expect "prune: a removed package is simply not found offline" 2 "$rc"
capture "$smith" build "-DURL=$url2" "-DHASH=sha256:$hex2"
expect "prune: and fetched again by a build that may fetch" "0 yes" "$rc $(exists "$packages/$hex2/file.txt")"

# 4. Pruning leaves what it must: fetches in progress, packages with no stamp, and removes what is orphaned or abandoned.
mkdir "$packages/.tmp-9999-extract" "$packages/.tmp-8888-extract"
: >"$packages/.tmp-9999-archive"
touch -t $old "$packages/.tmp-8888-extract"
mkdir "$packages/legacy"
echo legacy >"$packages/legacy/file.txt"
: >"$packages/orphan.used"
touch -t $old "$packages/orphan.used"
touch -t $old "$packages/prune.timestamp"
use "$url3" "$hex3"
expect "prune: a fetch in progress is kept" "yes yes" "$(exists "$packages/.tmp-9999-extract") $(exists "$packages/.tmp-9999-archive")"
expect "prune: an abandoned temporary folder older than 31 days is removed" no "$(exists "$packages/.tmp-8888-extract")"
expect "prune: a package with no stamp is kept" yes "$(exists "$packages/legacy/file.txt")"
expect "prune: and gets a stamp now" yes "$(exists "$packages/legacy.used")"
expect "prune: a stale stamp whose package is gone is removed" no "$(exists "$packages/orphan.used")"
touch -t $old "$packages/legacy.used" "$packages/prune.timestamp"
use "$url3" "$hex3"
expect "prune: a stamped package unused for 31 days is removed" no "$(exists "$packages/legacy")"
touch -t $old "$packages/prune.timestamp"
touch "$packages/$hex3.used"
use "$url3" "$hex3"
expect "prune: a package used just now is kept" yes "$(exists "$packages/$hex3/file.txt")"
touch -t $old "$packages/prune.timestamp"
capture "$smith" fetch "$url1"
expect "prune: smith fetch also checks, and keeps what it fetched" "0 yes" "$rc $(exists "$packages/$hex1/file.txt")"

# 5. Modules: one cache for every project, SDK modules built once. Two clones of a project, a cold build in each.
rm -rf "$XDG_CACHE_HOME"
"$smith" fetch "$url1" >/dev/null 2>&1
mkdir -p "$work/one/inc"
cd "$work/one" || exit 1
printf '#pragma once\nint value(void);\n' >inc/v.h
printf '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include "inc/v.h"\nint main(void) { printf("%%d\\n", value()); return 0; }\n' >main.c
printf '#include "inc/v.h"\nint value(void) { return 7; }\n' >value.c
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"inc", nullptr}});
    SmithObject main = {.compiler = "cc", .source = "main.c", .output = "build/main.o", .flags = smith_list(b, "-std=gnu23", nullptr)};
    SmithObject value = {.compiler = "cc", .source = "value.c", .output = "build/value.o", .flags = smith_list(b, "-std=gnu23", nullptr)};
    smith_object(b, &main);
    smith_object(b, &value);
    SmithProgram app = {.linker = "cc", .output = "build/app", .objects = (const SmithObject *[]){&main, &value, nullptr}};
    smith_program(b, &app);
    smith_default(b, app.step.outputs);
}
C
cp -R "$work/one" "$work/two"
cp -R "$work/one" "$work/three"
rm -rf "$work/two/.smith" "$work/three/.smith"
capture "$smith" build
expect "modules: the first project builds with a cold cache" "0 7" "$rc $(./build/app)"
count_modules() { find "$modules" -name '*.pcm' | wc -l | tr -d ' '; }
after_one=$(count_modules)
size_one=$(du -sk "$modules" | cut -f1)
expect "modules: the cache is the shared one and holds modules" yes "$([ "$after_one" -gt 0 ] && echo yes || echo no)"
capture "$smith" cache
expect_contains "modules: smith cache counts its entries" "$out" "modules: $modules"
sdk_before=$(find "$modules" -name '*.pcm' ! -name 'inc_*' ! -name 'smith_*' -exec stat -f '%N %m' {} + | sort | md5)
cd "$work/two" || exit 1
capture "$smith" build
expect "modules: the second clone builds" "0 7" "$rc $(./build/app)"
after_two=$(count_modules)
expect "modules: clang keeps its pruning timestamp in the shared cache" yes "$(exists "$modules/modules.timestamp")"
sdk_after=$(find "$modules" -name '*.pcm' ! -name 'inc_*' ! -name 'smith_*' -exec stat -f '%N %m' {} + | sort | md5)
expect "modules: the second clone's cold build did not rebuild the SDK modules (names and times unchanged)" "$sdk_before" "$sdk_after"
echo "info  modules: $after_one pcm files and $size_one kB after the first clone, $after_two and $(du -sk "$modules" | cut -f1) kB after the second"

# 6. Two worktrees with different headers at the same relative path do not collide.
cd "$work/three" || exit 1
printf '#include "inc/v.h"\nint value(void) { return 9; }\n' >value.c
printf '#pragma once\nint value(void);\nint extra(void);\n' >inc/v.h
printf '#include "inc/v.h"\nint extra(void) { return 1; }\n' >extra.c
capture "$smith" build
expect "modules: a worktree with a different inc/v.h builds" 0 "$rc"
expect "modules: and runs its own code" 9 "$(./build/app)"
cd "$work/one" || exit 1
capture "$smith" build
expect "modules: the first still builds after it" 0 "$rc"
expect "modules: and still runs its own code" 7 "$(./build/app)"
cd "$work/two" || exit 1
rm -rf build
capture "$smith" build
expect "modules: a clean build of the second still works" "0 7" "$rc $(./build/app)"

# 7. smith cache clean: one cache, the other, both.
capture "$smith" cache clean modules
expect "cache clean modules: exits 0" 0 "$rc"
expect_contains "cache clean modules: says what it removed" "$out" "removed $modules"
expect "cache clean modules: the modules are gone" no "$(exists "$modules")"
expect "cache clean modules: the packages stay" yes "$(exists "$packages")"
cd "$work/two" || exit 1
rm -rf build
capture "$smith" build
expect "cache clean: modules are built again after a clean" "0 7" "$rc $(./build/app)"
capture "$smith" cache clean packages
expect "cache clean packages: exits 0" 0 "$rc"
expect "cache clean packages: the packages are gone" no "$(exists "$packages")"
expect "cache clean packages: the modules stay" yes "$(exists "$modules")"
capture "$smith" cache clean
expect "cache clean: both" "0 no no" "$rc $(exists "$modules") $(exists "$packages")"
capture "$smith" cache
expect_contains "cache: empty again" "$out" "0 entries, 0 B"
capture "$smith" fetch "$url1"
expect "cache clean: a package is fetched again" "0 yes" "$rc $(exists "$packages/$hex1/file.txt")"

# 8. A running build holds the caches: smith cache clean refuses while it runs, and works after.
mkdir "$work/busy"
cd "$work/busy" || exit 1
cat >build.c <<'C'
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep wait = {.command = (const char *[]){"sh", "-c", "touch started; sleep 4; touch finished", nullptr}};
    smith_task(b, "wait", &wait);
    smith_default(b, smith_list(b, nullptr));
}
C
"$smith" build wait >/dev/null 2>&1 &
build_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
    [ -e started ] && break
    sleep 0.5
done
expect "busy: the build is running" yes "$(exists started)"
capture "$smith" cache clean
expect "busy: smith cache clean refuses while a build runs" 2 "$rc"
expect_contains "busy: and says why" "$err" "a smith build is using the cache"
expect "busy: nothing was removed" yes "$(exists "$packages/$hex1/file.txt")"
capture "$smith" cache
expect "busy: showing the caches is still allowed" 0 "$rc"
wait "$build_pid"
capture "$smith" cache clean
expect "busy: after the build, smith cache clean works" "0 no" "$rc $(exists "$packages")"

echo
echo "$total cache checks, $failures failed"
[ "$failures" -eq 0 ]

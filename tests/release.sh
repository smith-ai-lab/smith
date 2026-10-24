#!/bin/sh
# Tests for scripts/release.sh (issue #111) with no network: the package of the smith under test (checksum, name,
# contents), the pruning rule on fake tags, the guard in scratch git repositories, and publish and fetch against a stub gh
# that keeps its "releases" in a folder. Never touches a real repository.
# Usage: sh tests/release.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
script=$here/scripts/release.sh
# the package is xz of a 235 MB program: level 6 is 56 s of CPU, level 1 is 9 s, and the test is not about the ratio (#116)
export RELEASE_XZ_LEVEL=1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
failures=0
total=0
check() { # check <name> <expected> <actual>
    total=$((total + 1))
    if [ "$2" != "$3" ]; then
        failures=$((failures + 1))
        printf 'FAIL %s\n--- expected\n%s\n--- actual\n%s\n' "$1" "$2" "$3"
    fi
}

# 1. package: the name, the checksum and what is inside
os=$(uname -s | tr '[:upper:]' '[:lower:]')
name=smith-$os-$(uname -m).tar.xz
sh "$script" package "$smith" "$work/pkg" || check package ok failed
check package-files "$name
$name.sha256" "$(ls "$work/pkg")"
check package-checksum ok "$(cd "$work/pkg" && shasum -a 256 -c "$name.sha256" >/dev/null && echo ok)"
check package-contents "bin/
bin/smith
bin/smith-c++
bin/smith-cc" "$(tar -tJf "$work/pkg/$name" | sort)"
mkdir "$work/x" && tar -C "$work/x" -xJf "$work/pkg/$name"
check package-runs "$("$smith" version)" "$("$work/x/bin/smith" version)"
check package-link smith "$(readlink "$work/x/bin/smith-c++")"

# 2. prune: 30 fake tags, one a day from 2026-09-01, keep 20 and the newest of each ISO week
: >"$work/tags"
day=1
while [ $day -le 30 ]; do
    printf 'stage0-%02d 2026-09-%02dT10:00:00Z\n' $day $day >>"$work/tags"
    day=$((day + 1))
done
# the 21st oldest and older go, except the 6th (Sunday: the newest of week 36)
got=$(sh "$script" prune <"$work/tags" | sort | tr '\n' ' ')
check prune-30 "stage0-01 stage0-02 stage0-03 stage0-04 stage0-05 stage0-07 stage0-08 stage0-09 stage0-10 " "$got"
# ISO weeks run Monday to Sunday: Dec 29 to Jan 4 is one week (a, b, c), Dec 28 and 27 another (d, e)
check prune-week "stage0-b stage0-c stage0-e " "$(printf 'stage0-a 2026-01-04T00:00:00Z\nstage0-b 2026-01-03T00:00:00Z\nstage0-c 2025-12-29T00:00:00Z\nstage0-d 2025-12-28T00:00:00Z\nstage0-e 2025-12-27T00:00:00Z\n' |
    RELEASE_KEEP=1 sh "$script" prune | sort | tr '\n' ' ')"
check prune-few "" "$(head -5 "$work/tags" | sh "$script" prune)"

# 3. a scratch repository with a bare origin; a stub gh keeps releases in $work/gh
mkdir "$work/bin" "$work/gh" && cat >"$work/bin/gh" <<'STUB'
#!/bin/sh
echo "gh $*" >>"$GH_LOG"
case "$1 $2" in
"release view") [ -d "$GH_DIR/$3" ] ;;
"release create")
    tag=$3
    mkdir -p "$GH_DIR/$tag"
    shift 3
    while [ $# -gt 0 ]; do case $1 in --*) shift 2 ;; *) cp "$1" "$GH_DIR/$tag/"; shift ;; esac; done
    ;;
"release list") for d in "$GH_DIR"/*; do [ -d "$d" ] && printf '{"tagName":"%s","createdAt":"2026-10-01T00:00:00Z"}\n' "${d##*/}"; done ;;
"release download")
    tag=$3
    shift 3
    while [ $# -gt 0 ]; do case $1 in --dir) dir=$2; shift 2 ;; --pattern) pat="$pat $2"; shift 2 ;; *) shift ;; esac; done
    for p in $pat; do cp "$GH_DIR/$tag/$p" "$dir/"; done
    ;;
"release delete") rm -rf "$GH_DIR/$3" ;;
esac
STUB
chmod +x "$work/bin/gh"
export GH_LOG=$work/gh.log GH_DIR=$work/gh
: >"$GH_LOG"
git init -q --bare "$work/origin.git"
git init -q -b main "$work/repo" && cd "$work/repo" || exit 1
git config user.email t@t && git config user.name t
echo 1 >f && git add f && git commit -qm one && git remote add origin "$work/origin.git"
head=$(git rev-parse HEAD)
printf '#!/bin/sh\necho "smith 0.0 commit %s"\n' "$head" >"$work/fake-smith" && chmod +x "$work/fake-smith"
PATH=$work/bin:$PATH
export GITHUB_ACTIONS=

# guard: no origin/main yet, then a different tip, then a branch other than main
check refuse-unpushed "release: cannot fetch origin/main" "$(sh "$script" guard 2>&1 | tail -1)"
git push -q origin main
echo 2 >f && git commit -qam two
check refuse-ahead "release: refusing: HEAD is not origin/main" "$(sh "$script" publish "$work/fake-smith" 2>&1 | tail -1)"
git reset -q --hard origin/main && git checkout -qb topic
check refuse-branch "release: refusing: HEAD is not on main" "$(sh "$script" guard 2>&1 | tail -1)"
check refuse-no-gh-call "" "$(cat "$GH_LOG")"
git checkout -q main
sh "$script" guard && check guard-ok ok ok

# publish: the release holds the tarball and the checksum; prunes only stage0-*; a second run creates nothing
mkdir "$work/gh/stage0-old" "$work/gh/other-tag"
sh "$script" publish "$work/fake-smith" >/dev/null 2>&1 || check publish ok failed
check publish-assets "$name
$name.sha256" "$(ls "$work/gh/stage0-$head")"
check publish-creates 1 "$(grep -c 'release create' "$GH_LOG")"
sh "$script" publish "$work/fake-smith" >/dev/null 2>&1
check publish-twice 1 "$(grep -c 'release create' "$GH_LOG")"
check publish-keeps-others yes "$([ -d "$work/gh/other-tag" ] && [ -d "$work/gh/stage0-old" ] && echo yes)"
printf '#!/bin/sh\necho "smith 0.0 commit other"\n' >"$work/other-smith" && chmod +x "$work/other-smith"
check refuse-wrong-binary "release: $work/other-smith was not built from $head" "$(sh "$script" publish "$work/other-smith" 2>&1 | tail -1)"

# CI: only the commit of a push to main
check ci-refuses "release: CI releases only the commit of a push to main" \
    "$(GITHUB_ACTIONS=true GITHUB_REF=refs/pull/1/merge GITHUB_SHA=$head sh "$script" guard 2>&1)"
GITHUB_ACTIONS=true GITHUB_REF=refs/heads/main GITHUB_SHA=$head sh "$script" guard && check ci-ok ok ok

# fetch: download, checksum, extract, and the program runs with the commit
mkdir "$work/gh/stage0-real"
cp "$work/pkg/$name" "$work/pkg/$name.sha256" "$work/gh/stage0-real/"
sh "$script" fetch stage0-real "$work/dl" || check fetch ok failed
check fetch-runs "$("$smith" version)" "$("$work/dl/bin/smith" version)"
printf 'x' >>"$work/gh/stage0-real/$name"
check fetch-corrupt "release: the checksum of stage0-real does not match" "$(sh "$script" fetch stage0-real "$work/dl2" 2>&1 | tail -1)"

echo "release: $((total - failures))/$total passed"
[ $failures -eq 0 ]

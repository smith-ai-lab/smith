#!/bin/sh
# Tests for scripts/stage0.sh (issue #112) with no network: scratch git repositories and a stub gh that keeps its
# "releases" in a folder, each one made by scripts/release.sh package from a fake smith. Never touches a real
# repository. Usage: sh tests/stage0.sh path/to/smith (the argument is unused: the script is the same for every smith)
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
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

os=$(uname -s | tr '[:upper:]' '[:lower:]')
name=smith-$os-$(uname -m).tar.xz
mkdir "$work/bin" "$work/gh"
cat >"$work/bin/gh" <<'STUB'
#!/bin/sh
echo "gh $*" >>"$GH_LOG"
case "$1 $2" in
"release download")
    tag=$3
    shift 3
    pat=
    while [ $# -gt 0 ]; do case $1 in --dir) dir=$2; shift 2 ;; --pattern) pat="$pat $2"; shift 2 ;; *) shift ;; esac; done
    [ -d "$GH_DIR/$tag" ] || exit 1
    for p in $pat; do cp "$GH_DIR/$tag/$p" "$dir/"; done
    ;;
esac
STUB
chmod +x "$work/bin/gh"
export GH_LOG=$work/gh.log GH_DIR=$work/gh
: >"$GH_LOG"
PATH=$work/bin:$PATH
export PATH

# A release of a fake smith: `version` names the tag, `build` records its arguments and the smith-cc it would find first.
release() { # release <commit>
    printf '#!/bin/sh\ncase $1 in\nversion) echo "fake smith of %s" ;;\nbuild) echo "build $*" >"$STAGE0_OUT"; command -v smith-cc >>"$STAGE0_OUT" ;;\nesac\n' "$1" >"$work/fake-smith"
    chmod +x "$work/fake-smith"
    sh "$here/scripts/release.sh" package "$work/fake-smith" "$work/gh/stage0-$1" || check "package $1" ok failed
}

# The repository: a copy of the two scripts, five commits, a release for the second and a release for a commit that is no
# ancestor of HEAD (a branch); HEAD is two commits past the second.
git init -q --bare "$work/origin.git"
git init -q -b main "$work/repo" && cd "$work/repo" || exit 1
git config user.email t@t && git config user.name t && git remote add origin "$work/origin.git"
mkdir scripts && cp "$here/scripts/stage0.sh" "$here/scripts/release.sh" scripts/
git add scripts && git commit -qm zero
echo 1 >f && git add f && git commit -qm one
echo 2 >f && git commit -qam two
second=$(git rev-parse HEAD)
git checkout -q -b side && echo side >g && git add g && git commit -qm side && side=$(git rev-parse HEAD) && git checkout -q main
echo 3 >f && git commit -qam three && echo 4 >f && git commit -qam four
release "$second"
release "$side"
git tag "stage0-$second" "$second"
git tag "stage0-$side" "$side"
git push -q origin main side --tags
export STAGE0_OUT=$work/build.out

# The release of the newest ancestor is used (not the side branch's), the arguments reach `smith build`, and the
# smith-cc found first is the release's own.
out=$(sh scripts/stage0.sh -DX=1 2>&1)
check picks-newest-ancestor "stage0: downloading stage0-$second
fake smith of $second" "$(printf '%s\n' "$out" | head -2)"
check build-arguments "build build -DX=1" "$(head -1 "$STAGE0_OUT")"
check build-path "$work/repo/.smith/stage0/bin/smith-cc" "$(sed -n 2p "$STAGE0_OUT")"
check download-once 1 "$(grep -c 'release download' "$GH_LOG")"

# A second run finds the release unpacked and downloads nothing.
out=$(sh scripts/stage0.sh 2>&1)
check cached "stage0: stage0-$second is already in .smith/stage0" "$(printf '%s\n' "$out" | head -1)"
check cached-no-download 1 "$(grep -c 'release download' "$GH_LOG")"

# A newer ancestor with a release wins.
third=$(git rev-parse HEAD~1)
release "$third" && git tag "stage0-$third" "$third" && git push -q origin --tags
out=$(sh scripts/stage0.sh 2>&1)
check newer-release "stage0: downloading stage0-$third" "$(printf '%s\n' "$out" | head -1)"

# A checksum that does not match stops it.
rm -rf .smith && release "$third"
echo corrupt >>"$work/gh/stage0-$third/$name"
check bad-checksum yes "$(sh scripts/stage0.sh >"$work/bad.out" 2>&1 || echo yes)"
check bad-checksum-says "stage0: cannot get stage0-$third" "$(tail -1 "$work/bad.out")"

# No ancestor has a release.
git tag -d "stage0-$second" "stage0-$third" >/dev/null && git push -q origin ":refs/tags/stage0-$second" ":refs/tags/stage0-$third" && rm -rf .smith
out=$(sh scripts/stage0.sh 2>&1 || true)
case $out in *"no ancestor of HEAD has a release"*) check no-release ok ok ;; *) check no-release "the message" "$out" ;; esac

# SMITH_STAGE0_RELEASE names the release.
rm -rf .smith
out=$(SMITH_STAGE0_RELEASE=stage0-$second sh scripts/stage0.sh 2>&1)
check named "stage0: downloading stage0-$second" "$(printf '%s\n' "$out" | head -1)"

echo "$total stage0 checks, $failures failed"
[ "$failures" -eq 0 ]

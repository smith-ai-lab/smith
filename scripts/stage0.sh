#!/bin/sh
# Builds smith in a clone that has no smith (issue #112, knowledge/smith-bootstrap.md): smith is built by its previous
# release. Usage: scripts/stage0.sh [arguments of smith build]
#   1. finds the release `stage0-<commit>` of the newest ancestor commit of HEAD that has one (git tags, walking
#      `git rev-list HEAD`, after fetching the tags);
#   2. downloads it with `gh release download` (the repository is private; gh is signed in as it is to clone), checks
#      the SHA-256 and unpacks it into .smith/stage0/ (scripts/release.sh fetch);
#   3. runs .smith/stage0/bin/smith build, with that bin/ first on PATH so that its smith-cc is the CC of the build.
# It needs git, gh, tar, shasum, the network, and `~/.cache/smith/llvm` (`smith build llvm`, about 9 minutes once). With
# a smith installed, `smith build` does all of this already. SMITH_STAGE0_RELEASE=<tag> names the release to use.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"

die() {
    echo "stage0: $*" >&2
    exit 1
}

# The newest ancestor of HEAD with a release: the tags name commits, so the first commit of `git rev-list HEAD` that a
# tag names is the one. A shallow clone has no history to walk, so it is made whole first.
find_release() {
    if [ -n "${SMITH_STAGE0_RELEASE:-}" ]; then
        echo "$SMITH_STAGE0_RELEASE"
        return 0
    fi
    git fetch --quiet --tags --prune --prune-tags origin 2>/dev/null || echo "stage0: cannot fetch the tags; using the ones here" >&2
    if [ "$(git rev-parse --is-shallow-repository)" = true ]; then
        git fetch --quiet --unshallow origin || die "cannot fetch the history of a shallow clone"
    fi
    tags=$(mktemp)
    trap 'rm -f "$tags"' EXIT
    git tag --list 'stage0-*' | sed 's/^stage0-//' >"$tags"
    commit=$(git rev-list HEAD | grep -m 1 -F -x -f "$tags") || commit=
    [ -n "$commit" ] || die "no ancestor of HEAD has a release (stage0-<commit>): is the clone behind main? git pull, or ask for a release"
    echo "stage0-$commit"
}

tag=$(find_release)
folder=.smith/stage0
if [ "$(cat "$folder/tag" 2>/dev/null || true)" = "$tag" ] && [ -x "$folder/bin/smith" ]; then
    echo "stage0: $tag is already in $folder"
else
    echo "stage0: downloading $tag"
    rm -rf "$folder"
    mkdir -p "$folder"
    sh scripts/release.sh fetch "$tag" "$folder" || die "cannot get $tag"
    echo "$tag" >"$folder/tag"
fi
PATH="$here/$folder/bin:$PATH"
export PATH
"$folder/bin/smith" version
exec "$folder/bin/smith" build "$@"

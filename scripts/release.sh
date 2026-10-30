#!/bin/sh
# The stage0 release of smith (issue #111, knowledge/release.md): a GitHub Release `stage0-<full commit>` per merge to
# main, holding a smith-<os>-<arch>.tar.xz (bin/smith and the links smith-cc and smith-c++) and its .sha256 for each
# platform: each platform's job adds its own to a draft release, which `finish` publishes.
#   release.sh publish <smith>        guard, package, add to the draft release, prune (CI, `smith build release`)
#   release.sh finish <tag>           publish the draft release the jobs added to (CI's `publish` job)
#   release.sh package <smith> <dir>  write the tarball and its checksum into <dir>
#   release.sh fetch <tag> <dir>      download over plain HTTPS (curl, no gh), check the checksum, extract into <dir>
#   release.sh prune                  read "tag date" lines on stdin, print the tags to delete
#   release.sh guard                  succeed only when HEAD is main's tip
set -eu

keep=${RELEASE_KEEP:-20}
os=$(uname -s | tr '[:upper:]' '[:lower:]')
arch=$(uname -m)
name=smith-$os-$arch.tar.xz

die() {
    echo "release: $*" >&2
    exit 1
}

# By hand, HEAD must be on main and equal origin/main; the workflow's checkout of a push to main is that commit.
guard() {
    head=$(git rev-parse HEAD)
    if [ "${GITHUB_ACTIONS:-}" = true ]; then
        [ "${GITHUB_REF:-}" = refs/heads/main ] && [ "${GITHUB_SHA:-}" = "$head" ] ||
            die "CI releases only the commit of a push to main"
        return 0
    fi
    [ "$(git rev-parse --abbrev-ref HEAD)" = main ] || die "refusing: HEAD is not on main"
    git fetch --quiet origin main || die "cannot fetch origin/main"
    [ "$(git rev-parse origin/main)" = "$head" ] || die "refusing: HEAD is not origin/main"
}

package() {
    smith=$1
    out=$2
    [ -x "$smith" ] || die "$smith is not a program"
    stage=$(mktemp -d)
    mkdir -p "$stage/bin" "$out"
    cp "$smith" "$stage/bin/smith"
    ln -s smith "$stage/bin/smith-cc"
    ln -s smith "$stage/bin/smith-c++"
    if tar --version | grep -q bsdtar; then # macOS's
        tar --options "xz:compression-level=${RELEASE_XZ_LEVEL:-6}" -C "$stage" -cJf "$out/$name" bin
    else # GNU tar runs xz, which reads its level from XZ_OPT
        XZ_OPT=-${RELEASE_XZ_LEVEL:-6} tar -C "$stage" -cJf "$out/$name" bin
    fi
    rm -rf "$stage"
    (cd "$out" && shasum -a 256 "$name" >"$name.sha256")
}

# "tag date" lines in, the tags to delete out: keep the newest $keep and the newest of each ISO week.
prune() {
    while read -r tag date; do
        week=$(date -u -j -f '%Y-%m-%dT%H:%M:%SZ' "$date" +%G-W%V 2>/dev/null ||
            date -u -d "$date" +%G-W%V) || die "bad date $date"
        echo "$date $tag $week"
    done | sort -r | awk -v keep="$keep" '
        { n++; fresh = !($3 in seen); seen[$3] = 1; if (n > keep && !fresh) print $2 }'
}

# The draft release $1 at commit $2, made if missing. Jobs racing to make it can leave two drafts of one tag (GitHub
# allows that): each job then deletes every draft of the tag but the oldest, so one is left to upload into.
draft() {
    if ! gh release view "$1" >/dev/null 2>&1; then
        gh release create "$1" --draft --target "$2" --title "$1" \
            --notes "The smith of commit $2, for building the next commits (knowledge/release.md)." >/dev/null ||
            gh release view "$1" >/dev/null 2>&1 || die "cannot create the draft $1"
        for id in $(gh api 'repos/{owner}/{repo}/releases' --paginate \
            --jq ".[]|select(.draft and .tag_name==\"$1\")|.id" 2>/dev/null | sort -n | tail -n +2); do
            gh api -X DELETE "repos/{owner}/{repo}/releases/$id" >/dev/null 2>&1 || true
        done
    fi
}

# Publishes the draft $1 once every job has added its assets (CI's `publish` job). Immutable from here on.
finish() {
    gh release edit "$1" --draft=false || die "cannot publish $1"
    echo "release: published $1"
}

publish() {
    guard
    head=$(git rev-parse HEAD)
    tag=stage0-$head
    case $("$1" version) in *"commit $head") ;; *) die "$1 was not built from $head" ;; esac
    out=$(mktemp -d)
    package "$1" "$out"
    # One release holds every platform's tarball, and it is a draft until `release.sh finish` publishes it: the
    # repository's releases are immutable, so a published release takes no more assets. Each job adds its own pair to
    # the draft (--clobber makes a re-run replace its own files); the first creates the draft.
    draft "$tag" "$head"
    gh release upload "$tag" "$out/$name" "$out/$name.sha256" --clobber || die "cannot add $name to $tag"
    echo "release: added $name to the draft $tag"
    rm -rf "$out"
    # a failed prune leaves old releases, never a failed release
    delete=$(gh release list --limit 1000 --json tagName,createdAt \
        --jq '.[]|select(.tagName|startswith("stage0-"))|.tagName+" "+.createdAt' | prune) || delete=
    for old in $delete; do # another platform's job prunes the same tags: one already deleted is no failure
        gh release delete "$old" --cleanup-tag --yes ||
            ! gh release view "$old" >/dev/null 2>&1 || echo "release: could not delete $old" >&2
    done
}

fetch() {
    mkdir -p "$2"
    base=https://github.com/smith-ai-lab/smith/releases/download/$1
    for file in "$name" "$name.sha256"; do
        curl -fsSL --retry 3 -o "$2/$file" "$base/$file" || die "cannot download $base/$file"
    done
    (cd "$2" && shasum -a 256 -c "$name.sha256" >/dev/null) || die "the checksum of $1 does not match"
    tar -C "$2" -xJf "$2/$name"
}

case ${1:-} in
publish) publish "$2" ;;
finish) finish "$2" ;;
package) package "$2" "$3" ;;
fetch) fetch "$2" "$3" ;;
prune) prune ;;
guard) guard ;;
*) die "usage: release.sh publish <smith> | finish <tag> | package <smith> <dir> | fetch <tag> <dir> | prune | guard" ;;
esac

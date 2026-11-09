#!/usr/bin/env bash
# The download URLs of asset $1 (smith-tests-<os>.tar.gz or smith-imports-<os>.tar.gz) in the test-cache releases of
# the newest commit that published one (#2938): every job's of that commit, one URL a line. The commits are tried by
# their tags (`test-cache-<sha7>-<job>`, or `test-cache-<sha7>` before #2870), newest first along first parents from
# START (the commit under test in CI, GITHUB_SHA; main otherwise), at most 20, so no page of releases is read (the
# newest commit's fell past the first page, #2891). In a merge queue the first parent of a group is the group ahead of
# it, so a run reads the sets of the groups before it; in a pull request it is main.
# Says on stderr which commit it took and how many newer ones had none. Needs gh; GITHUB_REPOSITORY (smith's).
set -u
asset=$1
repo=${GITHUB_REPOSITORY:-smith-ai-lab/smith}
sha=$(gh api "repos/$repo/commits/${START:-${GITHUB_SHA:-main}}" --jq .sha 2>/dev/null)
newer=0
while [ -n "$sha" ] && [ "$newer" -lt 20 ]; do
    # a tag whose release was pruned answers 404, and gh prints its body: only URLs are kept
    urls=$(for tag in $(gh api "repos/$repo/git/matching-refs/tags/test-cache-${sha:0:7}" --jq '.[].ref|sub("refs/tags/";"")' 2>/dev/null); do
        gh api "repos/$repo/releases/tags/$tag" --jq ".assets[]|select(.name==\"$asset\")|.url" 2>/dev/null || true
    done | grep '^https://' || true)
    if [ -n "$urls" ]; then
        if [ "$newer" -eq 0 ]; then
            echo "$asset: commit ${sha:0:7}" >&2
        else
            echo "$asset: commit ${sha:0:7}, $newer first parents back (the newer have none)" >&2
        fi
        printf '%s\n' "$urls"
        exit 0
    fi
    sha=$(gh api "repos/$repo/commits/$sha" --jq '.parents[0].sha // empty' 2>/dev/null)
    newer=$((newer + 1))
done
echo "$asset: none in $newer commits" >&2

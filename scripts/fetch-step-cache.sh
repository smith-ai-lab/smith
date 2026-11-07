#!/bin/sh
# Downloads main's step cache (the artifact smith-steps-linux-<sha> of the CI `warm` job, #2690) into the cache folder
# of a fresh cloud container, so its first `smith build` compiles only what differs. Never fails: no artifact, no gh or
# a bad zip leaves the folder as it was and prints `step cache: none`.
repo=${GH_REPO:-smith-ai-lab/smith}
dir=${XDG_CACHE_HOME:-$HOME/.cache}/smith/steps
tmp=$(mktemp -d) || exit 0
trap 'rm -rf "$tmp"' EXIT
for sha in $(git rev-list --first-parent --max-count=20 origin/main 2>/dev/null); do
  id=$(gh api "repos/$repo/actions/artifacts?name=smith-steps-linux-$sha&per_page=5" \
    --jq '[.artifacts[]|select(.expired|not)][0].id // empty' 2>/dev/null)
  [ -n "$id" ] || continue
  if gh api "repos/$repo/actions/artifacts/$id/zip" >"$tmp/a.zip" 2>/dev/null &&
    unzip -o -q "$tmp/a.zip" -d "$tmp/steps" 2>/dev/null; then
    mkdir -p "$dir" && cp -R "$tmp/steps/." "$dir/" && { echo "step cache: $sha"; exit 0; }
  fi
done
echo "step cache: none"

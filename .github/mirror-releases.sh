#!/bin/sh
# Copies to GitLab's package registry every release file it lacks, under the
# release's version, and fails when GitLab holds a file with another hash or
# when either list cannot be read whole.
# Needs GH_TOKEN, REPOSITORY and GITLAB_PACKAGE_TOKEN in the environment, and
# GITLAB_API to name another GitLab than the real one and PAGE_SIZE to read
# its lists in smaller pages.
# Usage: mirror-releases.sh
set -eu

api=${GITLAB_API:-https://gitlab.com/api/v4/projects/smith-ai-lab%2Fsmith}
size=${PAGE_SIZE:-100}
work=$(mktemp -d)
mkdir -p "$work/files"

# Reads every page of a GitLab list, as one JSON array on each line, and
# fails if any page cannot be read.
pages() {
  page=1
  while :; do
    curl -fsS --header "DEPLOY-TOKEN: $GITLAB_PACKAGE_TOKEN" "${1}per_page=$size&page=$page" > "$work/page"
    jq -c . "$work/page"
    [ "$(jq length "$work/page")" -ge "$size" ] || break
    page=$((page + 1))
  done
}

# What GitHub holds, one line `<version> <file> <sha256>` for each file. One
# request reads a hundred releases.
gh api --paginate "repos/$REPOSITORY/releases?per_page=100" --jq \
  '.[] | (.tag_name | ltrimstr("v")) as $v | .assets[] | "\($v) \(.name) \(.digest | ltrimstr("sha256:"))"' \
  > "$work/github"

# What GitLab holds, in the same form.
pages "$api/packages?package_type=generic&package_name=smith&" > "$work/package-pages"
jq -r '.[] | "\(.id) \(.version)"' "$work/package-pages" > "$work/packages"
: > "$work/gitlab"
while read -r id version; do
  pages "$api/packages/$id/package_files?" > "$work/file-pages"
  jq -r ".[] | \"$version \\(.file_name) \\(.file_sha256)\"" "$work/file-pages" >> "$work/gitlab"
done < "$work/packages"

sh "$(dirname "$0")/plan-uploads.sh" "$work/github" "$work/gitlab" > "$work/plan"

while read -r version file hash; do
  mkdir -p "$work/files/$version"
  gh release download "v$version" --repo "$REPOSITORY" --pattern "$file" --dir "$work/files/$version"
  if [ "$(sha256sum "$work/files/$version/$file" | cut -d' ' -f1)" != "$hash" ]; then
    echo "the file $file of $version differs from the hash GitHub gives it" >&2
    exit 1
  fi
  curl -fsS --header "DEPLOY-TOKEN: $GITLAB_PACKAGE_TOKEN" \
    --upload-file "$work/files/$version/$file" \
    "$api/packages/generic/smith/$version/$file" > /dev/null
  echo "uploaded $file of $version"
done < "$work/plan"
echo "GitLab holds every file of every release"

#!/bin/sh
# Installs from the newest real release with the script in this checkout, and
# checks that the compiler it installs reports that release's version.
# Usage: check-install.sh
set -eu

home=$(mktemp -d)
trap 'rm -rf "$home"' EXIT
HOME=$home sh install.sh
tag=$(curl -fsSL -o /dev/null -w '%{url_effective}' https://github.com/smith-ai-lab/smith/releases/latest)
want="smith ${tag##*/v}"
got=$("$home/.local/bin/smith" --version)
[ "$got" = "$want" ] || { echo "installed '$got', the newest release is '$want'"; exit 1; }

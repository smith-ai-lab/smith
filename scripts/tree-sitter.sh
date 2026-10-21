#!/bin/sh
# Regenerates tree-sitter/src/parser.c (a developer step: users never need the tree-sitter CLI or Node, the plugin
# compiles the committed parser.c). Everything is pinned: the tree-sitter CLI by version and the sha256 of its release
# archive, tree-sitter-c by version and the sha256 of its source archive. Usage: scripts/tree-sitter.sh [corpus]
# `corpus` runs the grammar's corpus tests too. The CLI is kept in $XDG_CACHE_HOME/smith/tools (else ~/.cache/smith/tools).
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cli_version=0.27.1
cli_url=https://github.com/tree-sitter/tree-sitter/releases/download/v$cli_version/tree-sitter-macos-arm64.gz
cli_sha256=46cb97d9a2c52c94e22d9a76c2f1116156b8b61f98360e2a9cce05fcb197cd4d
c_url=https://github.com/tree-sitter/tree-sitter-c/archive/refs/tags/v0.24.2.tar.gz
c_hash=sha256:2eeb4db31f8fa0865e45488503d13403923bcb485a1bdb637abff8c42dd97364
abi=15 # what Neovim 0.12 loads (vim.treesitter.language_version)

tools=${XDG_CACHE_HOME:-$HOME/.cache}/smith/tools
mkdir -p "$tools"
cli=$tools/tree-sitter-$cli_version
if [ ! -x "$cli" ]; then
    curl -fsSL -o "$cli.gz" "$cli_url"
    actual=$(shasum -a 256 "$cli.gz" | cut -d' ' -f1)
    [ "$actual" = "$cli_sha256" ] || { echo "tree-sitter.sh: the CLI archive has sha256 $actual, not $cli_sha256" >&2; exit 1; }
    gunzip -c "$cli.gz" >"$cli.part" && chmod +x "$cli.part" && mv "$cli.part" "$cli" && rm "$cli.gz"
fi

fetched=$(smith fetch "$c_url" 2>&1)
case $fetched in *"$c_hash"*) ;; *) echo "tree-sitter.sh: tree-sitter-c does not match $c_hash: $fetched" >&2; exit 1 ;; esac
c_src=$(printf '%s\n' "$fetched" | sed -n 's/^smith: .* is in //p')

vendor=$here/tree-sitter/.vendor/tree-sitter-c
rm -rf "$vendor" && mkdir -p "$vendor"
cp "$c_src/grammar.js" "$vendor/"
cd "$here/tree-sitter"
"$cli" generate --abi "$abi"
if [ "${1:-}" = corpus ]; then "$cli" test; fi

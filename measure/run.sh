#!/bin/sh
# Times the programs that decide how fast a Smith program is, and prints one line for each.
# Usage: sh measure/run.sh [push|lexer]...   (no word runs both)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
cargo build --release --locked --manifest-path "$here/../bootstrap/Cargo.toml"
cargo run --release --manifest-path "$here/Cargo.toml" -- "$@"

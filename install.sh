{
  # Installs the newest Smith compiler into ~/.local/bin on macOS or Linux.
  #   curl -fsSL https://raw.githubusercontent.com/smith-ai-lab/smith/main/install.sh | sh
  # Downloads the file for this machine from the newest release, checks it against
  # the release's SHA256SUMS, and only then puts it in place.
  # SMITH_RELEASE_URL names the folder the release's files are in, so that a test
  # can use a folder on disk. It is not a way to choose a version.
  # The whole script is one group, and the group is the first thing in the file,
  # so a script cut off part way is a syntax error that runs nothing, and `sh`
  # gives a non-zero status. Nothing outside the group may be added.
  set -eu

  base=${SMITH_RELEASE_URL:-https://github.com/smith-ai-lab/smith/releases/latest/download}

  system=$(uname -s)
  chip=$(uname -m)
  case "$system" in
    Darwin) os=macos ;;
    Linux) os=linux ;;
    *) os= ;;
  esac
  case "$chip" in
    arm64 | aarch64) arch=arm64 ;;
    x86_64 | amd64) arch=x64 ;;
    *) arch= ;;
  esac
  if [ -z "$os" ] || [ -z "$arch" ]; then
    echo "smith: no compiler for $system on $chip" >&2
    exit 1
  fi
  file=smith-${os}_${arch}

  if command -v sha256sum >/dev/null 2>&1; then
    hash_of() { sha256sum "$1" | cut -d ' ' -f 1; }
  else
    hash_of() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
  fi

  work=$(mktemp -d)
  trap 'rm -rf "$work"; [ -z "${dir:-}" ] || rm -f "$dir/.smith.new"' EXIT HUP INT TERM

  curl -fsSL -o "$work/$file" "$base/$file"
  curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS"

  want=$(awk -v f="$file" '$2 == f || $2 == "*" f { print $1 }' "$work/SHA256SUMS")
  if [ -z "$want" ] || [ "$(hash_of "$work/$file")" != "$want" ]; then
    echo "smith: $file does not match its hash in SHA256SUMS, so nothing was installed" >&2
    exit 1
  fi

  dir=$HOME/.local/bin
  mkdir -p "$dir"
  # Copied next to its place and then renamed, so a smith already there is never
  # half replaced.
  cp "$work/$file" "$dir/.smith.new"
  chmod +x "$dir/.smith.new"
  mv -f "$dir/.smith.new" "$dir/smith"
  echo "smith: installed $("$dir/smith" --version) in $dir"

  case ":$PATH:" in
    *":$dir:"*) ;;
    *)
      echo
      echo "$dir is not on your PATH. Add this line to your shell's startup file:"
      echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
      ;;
  esac
}

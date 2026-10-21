#!/bin/sh
# Tests of the Neovim plugin (issue #24: plugin/smith.lua and editors/neovim) in headless Neovim; skipped, with a message,
# when nvim is absent. Each test of tests/neovim.lua runs in a Neovim of its own, started as a user's would be: the
# repository on the runtimepath and `runtime plugin/smith.lua`, in a scratch home (XDG_*) so that the user's
# configuration and data are never read or written. The plugin compiles the parser with the smith under test.
# Usage: sh tests/neovim.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
nvim=$(command -v nvim || true)
if [ -z "$nvim" ]; then
    echo "SKIP  nvim is not installed, so the Neovim plugin is not tested"
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
failures=0
total=0

mkdir "$work/bin" && ln -s "$smith" "$work/bin/smith" && ln -s "$smith" "$work/bin/smith-cc" && ln -s "$smith" "$work/bin/smith-c++"
export XDG_DATA_HOME=$work/data XDG_CONFIG_HOME=$work/config XDG_STATE_HOME=$work/state XDG_CACHE_HOME=$work/cache
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME"

# A scratch project for the language server, and a nvim-dap that is only a table.
mkdir "$work/project" && printf 'int main(void) { return 0; }\n' >"$work/project/main.c" && : >"$work/project/build.c"
mkdir -p "$work/fakedap/lua" && printf 'return { adapters = {}, configurations = {} }\n' >"$work/fakedap/lua/dap.lua"

# A smith that prints a commit of our choosing and is the real one otherwise.
shim() { # folder, version line
    mkdir -p "$1"
    printf '#!/bin/sh\nif [ "$1" = --smith-version ]; then echo "%s"; exit 0; fi\nexec "%s" "$@"\n' "$2" "$smith" >"$1/smith"
    chmod +x "$1/smith"
}
head=$(git -C "$here" rev-parse HEAD 2>/dev/null || true)
shim "$work/same" "smith 0.0 (x) commit $head"
shim "$work/different" "smith 0.0 (x) commit 0123456789abcdef0123456789abcdef01234567"
shim "$work/unknown" "smith 0.0 (x)"

# one NAME PATH [nvim arguments]: runs test NAME with PATH, prints its line, counts it.
one() {
    name=$1 path=$2
    shift 2
    case " ${SMITH_NVIM_ONLY:-$name} " in *" $name "*) ;; *) return ;; esac # SMITH_NVIM_ONLY="a b" runs just those
    total=$((total + 1))
    result=$(env PATH="$path" SMITH_NVIM_TEST="$name" SMITH_REPO="$here" SMITH_PROJECT="$work/project" \
        "$nvim" --headless -u NONE -i NONE --cmd "set runtimepath^=$here" --cmd "filetype on" --cmd "let g:smith_no_version_check = 1" \
        "$@" --cmd "runtime plugin/smith.lua" \
        -c "lua vim.api.nvim_create_autocmd('VimEnter', {once = true, callback = function() vim.schedule(function() dofile('$here/tests/neovim.lua') end) end})" 2>&1)
    status=$?
    last=$(printf '%s\n' "$result" | grep -E '^(ok|FAIL)  ' | tail -1)
    if [ "$status" -eq 0 ] && [ "${last#ok }" != "$last" ]; then
        echo "$last"
    else
        failures=$((failures + 1))
        echo "FAIL  $name (status $status)"
        printf '%s\n' "$result" | sed 's/^/      /'
    fi
}

base=$work/bin:/usr/bin:/bin
one filetype "$base"
one parser "$base"
one highlights "$base"
one highlight_buffer "$base"
one plain_c "$base"
one lsp_config "$base"
one lsp_attach "$base"

# nvim-dap on the runtimepath: the adapter registers at startup; a user's own adapter and configurations stay.
one dap_registers "$base" --cmd "set runtimepath+=$work/fakedap"
one dap_keeps_the_users "$base" --cmd "set runtimepath+=$work/fakedap" --cmd "lua package.loaded.dap = { adapters = { smith = 'mine' }, configurations = { c = { { name = 'user c' } } } }"
one dap_absent "$base"

# The commit of the plugin against the commit smith was built from.
if [ -n "$head" ]; then
    one version_same "$work/same:$base"
    one version_different "$work/different:$base"
    one version_unknown "$work/unknown:$base"
    one version_at_startup "$work/different:$base" --cmd "lua _G.smith_notices = {}; vim.notify = function(message) table.insert(_G.smith_notices, message) end" --cmd "unlet g:smith_no_version_check"
else
    echo "SKIP  the version tests need a git checkout of smith"
fi
XDG_DATA_HOME=$work/empty-data one no_smith "/usr/bin:/bin"

# A smith built from a checkout names its commit, which is the plugin's way to compare.
version=$("$smith" --smith-version)
case $version in
*" commit "*)
    total=$((total + 1))
    commit=${version##* commit }
    case $commit in
    *[!0-9a-f]* | "") failures=$((failures + 1)); echo "FAIL  smith --smith-version names a commit: $version" ;;
    *) echo "ok    smith --smith-version names its commit" ;;
    esac
    ;;
*) echo "SKIP  this smith was built without a commit: $version" ;;
esac

echo "$total tests, $failures failed"
[ "$failures" -eq 0 ]

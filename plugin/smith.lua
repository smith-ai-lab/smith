-- The only Neovim file at the root of the smith repository (issue #24), as fzf keeps plugin/fzf.vim at the root of a Go
-- repo: install with vim.pack.add({ "https://github.com/adam-r-kowalski/smith" }). It registers the .smith file type,
-- puts editors/neovim (the Lua code and the queries) on the runtimepath and loads require("smith"). It enables nothing:
-- vim.lsp.enable("smith") is the user's choice, and the debug adapter only registers.
if vim.g.loaded_smith then return end
vim.g.loaded_smith = true

local here = debug.getinfo(1, "S").source:sub(2)
local root = vim.fs.dirname(vim.fs.dirname(vim.uv.fs_realpath(here) or here))

vim.filetype.add({ extension = { smith = "smith" } })
vim.opt.runtimepath:append(root .. "/editors/neovim")
require("smith").load(root)

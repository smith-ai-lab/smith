-- `smith lsp` is clangd from the LLVM smith compiles with. This only defines the config; the user turns it on with
-- vim.lsp.enable("smith") (it replaces a clangd config: do not enable both). Point no --query-driver at it: with
-- it clangd puts the SDK's include paths ahead of clang's own headers and the SDK's modules fail.
local M = {}

M.config = {
  cmd = { "smith", "lsp" },
  filetypes = { "c", "cpp", "objc", "objcpp", "smith" },
  root_markers = { "build.c", "build.smith", "main.smith", "main.c", "compile_commands.json", ".git" },
}

function M.define()
  if vim.lsp and vim.lsp.config then vim.lsp.config("smith", M.config) end
end

return M

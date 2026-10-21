-- Neovim support for .smith and smith's C (issue #24). plugin/smith.lua at the repository root loads this.
--
--   .smith files   the "smith" file type, and highlighting from the tree-sitter grammar in tree-sitter/ (queries in
--                  queries/smith). Neovim 0.12 loads parsers itself; the parser is the committed tree-sitter/src/parser.c,
--                  compiled on first use with `smith cc -shared` into stdpath("data")/smith/parser.
--   smith lsp      a "smith" config for vim.lsp, which the user turns on with vim.lsp.enable("smith").
--   .h files       the language of the project, in a smith project only (lua/smith/filetype.lua).
--   smith dap      a "smith" adapter for nvim-dap when it is installed.
--   version        a warning, once, when the plugin and the installed smith are different commits.
local M = {}

M.root = nil -- the repository the plugin was loaded from
M.parser_name = "smith"

local pending = nil -- callbacks waiting for the parser to be compiled
local loaded = false
local warned = false

local function notify_once(message)
  if warned then return end
  warned = true
  vim.schedule(function() vim.notify("smith: " .. message, vim.log.levels.WARN) end)
end

local function read(path)
  local file = io.open(path, "rb")
  if not file then return nil end
  local text = file:read("a")
  file:close()
  return text
end

-- Where the compiled parser lives: named for the contents of parser.c, so a new parser is compiled once and an old
-- one is never loaded again.
function M.parser_source() return M.root .. "/tree-sitter/src/parser.c" end

function M.parser_path()
  local text = read(M.parser_source())
  if not text then return nil end
  return vim.fn.stdpath("data") .. "/smith/parser/smith-" .. vim.fn.sha256(text):sub(1, 16) .. ".so"
end

local function add_language(path)
  local ok, err = pcall(vim.treesitter.language.add, M.parser_name, { path = path, symbol_name = "smith" })
  if ok then
    vim.treesitter.language.register(M.parser_name, "smith")
    loaded = true
  end
  return ok, err
end

local function finish(ok, value)
  local waiting = pending or {}
  pending = nil
  for _, callback in ipairs(waiting) do callback(ok, value) end
end

-- Compiles parser.c with `smith cc -shared` when there is no parser for it yet, then registers the language; calls
-- callback(true, path) or callback(false, message) when done (at once if the parser is already loaded).
function M.ensure_parser(callback)
  if loaded then return callback(true, M.parser_path()) end
  if pending then return table.insert(pending, callback) end
  local path = M.parser_path()
  if not path then return callback(false, "tree-sitter/src/parser.c is missing from " .. tostring(M.root)) end
  if vim.uv.fs_stat(path) then
    local ok, err = add_language(path)
    return callback(ok, ok and path or tostring(err))
  end
  local smith = vim.fn.exepath("smith")
  if smith == "" then
    return callback(false, "smith is not on the PATH, so the .smith parser cannot be compiled (smith cc -shared)")
  end
  pending = { callback }
  vim.fn.mkdir(vim.fs.dirname(path), "p")
  local partial = path .. ".part"
  local command = {
    smith, "cc", "-shared", "-fPIC", "-O2", "-I", M.root .. "/tree-sitter/src", M.parser_source(), "-o", partial,
  }
  vim.system(command, { text = true }, function(result)
    vim.schedule(function()
      if result.code ~= 0 then
        return finish(false, "compiling the .smith parser failed: " .. (result.stderr or ""))
      end
      vim.uv.fs_rename(partial, path)
      for _, old in ipairs(vim.fn.glob(vim.fs.dirname(path) .. "/smith-*.so", false, true)) do
        if old ~= path then vim.uv.fs_unlink(old) end
      end
      local ok, err = add_language(path)
      finish(ok, ok and path or tostring(err))
    end)
  end)
end

-- Highlights a .smith buffer with the grammar (compiling the parser first when it is the first use).
function M.start(buffer)
  M.ensure_parser(function(ok, message)
    if not ok then return notify_once(message) end
    if vim.api.nvim_buf_is_valid(buffer) and vim.bo[buffer].filetype == "smith" then
      pcall(vim.treesitter.start, buffer, M.parser_name)
    end
  end)
end

-- Warns, once, when the plugin's commit is not the commit the installed smith was built from. Both are read
-- asynchronously; callback(state) is called for tests with "same", "different" or the reason nothing was compared.
function M.check_version(callback)
  callback = callback or function() end
  local smith = vim.fn.exepath("smith")
  if smith == "" then return callback("no smith") end
  vim.system({ "git", "-C", M.root, "rev-parse", "HEAD" }, { text = true }, function(plugin)
    local mine = plugin.code == 0 and vim.trim(plugin.stdout) or nil
    if not mine then return callback("not a checkout") end
    vim.system({ smith, "version" }, { text = true }, function(binary)
      local theirs = binary.code == 0 and (binary.stdout or ""):match("commit (%x+)") or nil
      if not theirs then return callback("smith has no commit") end
      if theirs == mine then return callback("same") end
      notify_once(
        ("the plugin is at %s and the installed smith was built from %s; update both "
          .. "(vim.pack.update, then smith build install in a checkout)"):format(mine:sub(1, 9), theirs:sub(1, 9))
      )
      callback("different")
    end)
  end)
end

-- Called by plugin/smith.lua.
function M.load(root)
  M.root = root
  local group = vim.api.nvim_create_augroup("smith", { clear = true })
  vim.api.nvim_create_autocmd("FileType", {
    group = group,
    pattern = "smith",
    callback = function(event) M.start(event.buf) end,
  })
  require("smith.lsp").define()
  require("smith.filetype").register()
  local dap = require("smith.dap")
  vim.api.nvim_create_autocmd("FileType", {
    group = group,
    pattern = { "c", "cpp", "objc", "objcpp", "smith" },
    callback = function() dap.register() end,
  })
  local function after_start()
    dap.register()
    if not vim.g.smith_no_version_check then M.check_version() end
  end
  if vim.v.vim_did_enter == 1 then
    after_start()
  else
    vim.api.nvim_create_autocmd("VimEnter", { group = group, once = true, callback = after_start })
  end
end

return M

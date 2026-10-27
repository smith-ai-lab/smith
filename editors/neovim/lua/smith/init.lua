-- Neovim support for .smith and smith's C (issue #24). plugin/smith.lua at the repository root loads this.
--
--   .smith files   the "smith" file type, and highlighting from the tree-sitter grammar in tree-sitter/ (queries in
--                  queries/smith). Neovim 0.12 loads parsers itself; the parser is the one the installed smith carries
--                  (`smith cc --smith-tree-sitter` prints its path), so the grammar always matches the smith.
--   smith lsp      a "smith" config for vim.lsp, which the user turns on with vim.lsp.enable("smith").
--   .h files       the language of the project, in a smith project only (lua/smith/filetype.lua).
--   smith dap      a "smith" adapter for nvim-dap when it is installed.
--   version        a warning, once, when the plugin and the installed smith are different commits.
local M = {}

M.root = nil -- the repository the plugin was loaded from
M.parser_name = "smith"

local pending = nil -- callbacks waiting for smith to name the parser
local loaded = false -- the path of the parser once it is registered
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

local function add_language(path)
  local ok, err = pcall(vim.treesitter.language.add, M.parser_name, { path = path, symbol_name = "smith" })
  if ok then
    vim.treesitter.language.register(M.parser_name, "smith")
    loaded = path
  end
  return ok, err
end

local function finish(ok, value)
  local waiting = pending or {}
  pending = nil
  for _, callback in ipairs(waiting) do callback(ok, value) end
end

-- Asks the installed smith for the path of its parser (`smith cc --smith-tree-sitter`: the library it carries, unpacked
-- on first use) and registers the language; calls callback(true, path) or callback(false, message) when done (at once
-- if the parser is already loaded).
function M.ensure_parser(callback)
  if loaded then return callback(true, loaded) end
  if pending then return table.insert(pending, callback) end
  local smith = vim.fn.exepath("smith")
  if smith == "" then
    return callback(false, "smith is not on the PATH, so the .smith parser cannot be found (smith cc --smith-tree-sitter)")
  end
  pending = { callback }
  vim.system({ smith, "cc", "--smith-tree-sitter" }, { text = true }, function(result)
    vim.schedule(function()
      local path = vim.trim(result.stdout or "")
      if result.code ~= 0 or path == "" then
        return finish(false, "smith cannot give the .smith parser (smith cc --smith-tree-sitter): " .. vim.trim(result.stderr or ""))
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

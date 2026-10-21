-- `smith dap` is lldb-dap from the LLVM smith compiles with. When nvim-dap is installed this registers a "smith"
-- adapter and a launch configuration for C and .smith, and nothing else: it never replaces an adapter or a list of
-- configurations that exists (a user's own, set before this runs, wins; one set after simply replaces ours).
local M = {}

M.filetypes = { "c", "cpp", "objc", "objcpp", "smith" }

local function launch()
  return {
    type = "smith",
    request = "launch",
    name = "smith: launch a program",
    program = function() return vim.fn.input("Program: ", vim.fn.getcwd() .. "/", "file") end,
    cwd = "${workspaceFolder}",
  }
end

local function installed()
  if package.loaded["dap"] then return true end
  return #vim.api.nvim_get_runtime_file("lua/dap.lua", false) > 0
    or #vim.api.nvim_get_runtime_file("lua/dap/init.lua", false) > 0
end

-- True when the adapter is registered (nvim-dap is on the runtimepath or already loaded).
function M.register()
  if not installed() then return false end
  local ok, dap = pcall(require, "dap")
  if not ok then return false end
  dap.adapters = dap.adapters or {}
  if dap.adapters.smith == nil then
    dap.adapters.smith = { type = "executable", command = "smith", args = { "dap" } }
  end
  dap.configurations = dap.configurations or {}
  for _, filetype in ipairs(M.filetypes) do
    if dap.configurations[filetype] == nil then dap.configurations[filetype] = { launch() } end
  end
  return true
end

return M

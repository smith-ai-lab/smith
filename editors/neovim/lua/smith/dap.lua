-- `smith dap` is lldb-dap from the LLVM smith compiles with. When nvim-dap is installed this registers a "smith"
-- adapter and two launch configurations for C and .smith: build the project with `smith build` and launch its program
-- (the one `smith run` runs), or ask for a program's path. It does nothing else: it never replaces an adapter or a list
-- of configurations that exists (a user's own, set before this runs, wins; one set after simply replaces ours).
local M = {}

M.filetypes = { "c", "cpp", "objc", "objcpp", "smith" }

local function prompt()
  return {
    type = "smith",
    request = "launch",
    name = "smith: launch a program",
    program = function() return vim.fn.input("Program: ", vim.fn.getcwd() .. "/", "file") end,
    cwd = "${workspaceFolder}",
  }
end

-- The folder of the project the current buffer (else the working directory) is in: the one with build.c or build.smith.
function M.project_root()
  local markers = { "build.c", "build.smith" }
  local name = vim.api.nvim_buf_get_name(0)
  return (name ~= "" and vim.fs.root(name, markers)) or vim.fs.root(vim.fn.getcwd(), markers)
end

-- The program `smith run` runs, from the text of .smith/build.info (`program<TAB>path`, `default<TAB>target`): the one
-- program that is among the defaults. Returns the path, or nil and `smith run`'s message when there is none or more
-- than one.
function M.program_of(text)
  local programs, defaults = {}, {}
  for line in text:gmatch("[^\n]+") do
    local kind, value = line:match("^([^\t]+)\t([^\t]+)")
    if kind == "program" then
      table.insert(programs, value)
    elseif kind == "default" then
      table.insert(defaults, value)
    end
  end
  local named = {}
  for _, target in ipairs(defaults) do
    for _, program in ipairs(programs) do
      if target == program then table.insert(named, target) end
    end
  end
  if #named == 1 then return named[1] end
  return nil,
    ("the default of build.c names %s program%s\nsmith run runs the program that smith_default names, so name exactly one"):format(
      #named == 0 and "no" or "more than one",
      #defaults > 0 and ": " .. table.concat(defaults, ", ") or ""
    )
end

local function fail(message)
  vim.notify("smith: " .. message, vim.log.levels.ERROR)
  return require("dap").ABORT
end

-- Runs `smith build` in `root`; callback(ok, output) is called on the main loop.
local function build(root, callback)
  vim.system({ "smith", "build" }, { cwd = root, text = true }, function(result)
    vim.schedule(function()
      local output = (result.stderr or "") .. (result.stdout or "")
      callback(result.code == 0, output ~= "" and output or ("smith build failed (status " .. result.code .. ")"))
    end)
  end)
end

-- Builds and waits. Inside a coroutine (nvim-dap calls `program` in one) it yields, so the editor stays free; anywhere
-- else it waits with vim.wait.
local function build_and_wait(root)
  local ok, output, done = false, "", false
  local thread = coroutine.running()
  local waiting = thread ~= nil and coroutine.isyieldable()
  build(root, function(success, text)
    ok, output, done = success, text, true
    if waiting then coroutine.resume(thread) end
  end)
  if waiting then
    if not done then coroutine.yield() end
  else
    vim.wait(600000, function() return done end, 20)
  end
  return ok, output
end

-- The absolute path of the project's program after building it; dap.ABORT, with the error shown, when there is no
-- project, the build fails or the build names no single program.
function M.build_program()
  local root = M.project_root()
  if not root then return fail("there is no build.c or build.smith above this file or the working directory") end
  local ok, output = build_and_wait(root)
  if not ok then return fail("smith build failed in " .. root .. "\n" .. output) end
  local file = io.open(root .. "/.smith/build.info", "rb")
  if not file then return fail("the build left no .smith/build.info in " .. root) end
  local text = file:read("a")
  file:close()
  local program, message = M.program_of(text)
  if not program then return fail(message) end
  if program:sub(1, 1) == "/" then return program end
  return vim.fs.normalize(root .. "/" .. program)
end

local function build_and_launch()
  return {
    type = "smith",
    request = "launch",
    name = "smith: build and launch the program",
    program = M.build_program,
    cwd = function() return M.project_root() or vim.fn.getcwd() end,
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
    if dap.configurations[filetype] == nil then dap.configurations[filetype] = { build_and_launch(), prompt() } end
  end
  return true
end

return M

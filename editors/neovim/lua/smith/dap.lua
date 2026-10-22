-- `smith dap` is lldb-dap from the LLVM smith compiles with. When nvim-dap is installed this registers a "smith"
-- adapter and one launch configuration for C and .smith, so starting a session asks nothing: in a smith project it
-- builds with `smith build` and launches the program `smith run` runs, or, with the cursor inside a `test_` function,
-- the project's test runner for that one test (`--test <name> --in-process`); it asks for a program's path only where
-- there is no project or the build names no single program. It does nothing else: it never replaces an adapter or a list of
-- configurations that exists (a user's own, set before this runs, wins; one set after simply replaces ours).
local M = {}

M.filetypes = { "c", "cpp", "objc", "objcpp", "smith" }

-- The folder of the project the current buffer (else the working directory) is in: the one with a build file, or a
-- main.smith or main.c (smith/project.lua).
function M.project_root()
  local project = require("smith.project")
  local name = vim.api.nvim_buf_get_name(0)
  return (name ~= "" and project.root(name)) or project.root(vim.fn.getcwd())
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

-- Asks for the program's path, starting in `folder`; an empty answer aborts the launch.
local function ask(folder)
  local path = vim.fn.input("Program: ", folder .. "/", "file")
  if path == "" then return require("dap").ABORT end
  return vim.fs.normalize(vim.fn.fnamemodify(path, ":p"))
end

-- Runs `smith build` in `root`; callback(ok, output) is called on the main loop.
local function build(root, target, callback)
  local command = { "smith", "build" }
  if target then table.insert(command, target) end
  vim.system(command, { cwd = root, text = true }, function(result)
    vim.schedule(function()
      local output = (result.stderr or "") .. (result.stdout or "")
      callback(result.code == 0, output ~= "" and output or ("smith build failed (status " .. result.code .. ")"))
    end)
  end)
end

-- Builds and waits. Inside a coroutine (nvim-dap calls `program` in one) it yields, so the editor stays free; anywhere
-- else it waits with vim.wait.
local function build_and_wait(root, target)
  local ok, output, done = false, "", false
  local thread = coroutine.running()
  local waiting = thread ~= nil and coroutine.isyieldable()
  build(root, target, function(success, text)
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

-- The text of .smith/build.info of `root`, or nil.
local function read_info(root)
  local file = io.open(root .. "/.smith/build.info", "rb")
  if not file then return nil end
  local text = file:read("a")
  file:close()
  return text
end

-- The absolute path of the program to launch. In a smith project it builds first, and a failed build shows its output
-- and aborts (dap.ABORT); then it is the program `smith run` runs. Where there is no project, or the build names no
-- single program (the reason is shown), it asks for the path.
function M.program()
  local root = M.project_root()
  if not root then return ask(vim.fn.getcwd()) end
  local ok, output = build_and_wait(root)
  if not ok then return fail("smith build failed in " .. root .. "\n" .. output) end
  local text = read_info(root)
  if not text then return fail("the build left no .smith/build.info in " .. root) end
  local program, message = M.program_of(text)
  if not program then
    vim.notify("smith: " .. message, vim.log.levels.WARN)
    return ask(root)
  end
  if program:sub(1, 1) == "/" then return program end
  return vim.fs.normalize(root .. "/" .. program)
end

-- The runner of the tests, from the `runner<TAB>path` line of the text of .smith/build.info (a build.info without one
-- names none): the path as written there.
function M.runner_of(text)
  return text:match("\nrunner\t([^\t\n]+)") or text:match("^runner\t([^\t\n]+)")
end

local function absolute(root, path)
  if path:sub(1, 1) == "/" then return path end
  return vim.fs.normalize(root .. "/" .. path)
end

-- The name of the function the cursor of `buffer` is in, from the tree-sitter parse: nil when there is no parser or
-- the cursor is in no function.
local function function_name_by_tree(buffer, row, col)
  local ok, node = pcall(vim.treesitter.get_node, { bufnr = buffer, pos = { row, col } })
  if not ok or not node then return nil end
  while node and node:type() ~= "function_definition" do
    node = node:parent()
  end
  if not node then return nil end
  local declarator = node:field("declarator")[1]
  for _ = 1, 8 do -- bounded: pointer and function declarators wrap the name
    if not declarator or declarator:type() == "identifier" then break end
    declarator = declarator:field("declarator")[1] or declarator:named_child(0)
  end
  if declarator and declarator:type() == "identifier" then return vim.treesitter.get_node_text(declarator, buffer) end
  return nil
end

-- Without a parser: the nearest definition above the cursor, a line that starts at column 0 with a type and a name
-- followed by `(`.
local function function_name_by_text(buffer, row)
  local lines = vim.api.nvim_buf_get_lines(buffer, 0, row + 1, false)
  for line = #lines, 1, -1 do
    local text = lines[line]
    if text:match("^[%a_]") and not text:match("^%s") and text:find("(", 1, true) then
      return text:match("([%a_][%w_]*)%s*%(")
    end
  end
  return nil
end

-- The `module a::b;` line among `lines`, as `a::b`; nil when there is none.
function M.module_of_lines(lines)
  for _, text in ipairs(lines) do
    local name = text:match("^%s*module%s+([%w_:]+)%s*;")
    if name then return name end
  end
  return nil
end

-- The name `--test` takes for the function `name` in a file with these `lines`: `test_x`, or `a::b::test_x` in a module.
function M.test_name(lines, name)
  local module = M.module_of_lines(lines)
  return module and (module .. "::" .. name) or name
end

-- The name `--test` takes for the test the cursor is in (`test_x`, or `a::b::test_x` in a `.smith` module), or nil when
-- the cursor is not inside a function whose name starts with `test_` in a `c` or `smith` buffer.
function M.test_under_cursor()
  local buffer = vim.api.nvim_get_current_buf()
  if not vim.tbl_contains({ "c", "smith" }, vim.bo[buffer].filetype) then return nil end
  local position = vim.api.nvim_win_get_cursor(0)
  local row, col = position[1] - 1, position[2]
  local name = function_name_by_tree(buffer, row, col) or function_name_by_text(buffer, row)
  if not name or name:sub(1, 5) ~= "test_" then return nil end
  return M.test_name(vim.api.nvim_buf_get_lines(buffer, 0, -1, false), name)
end

-- Builds the tests' runner and returns its absolute path: first `smith build smith_needs`, which runs build.c and so
-- writes .smith/build.info, then the runner itself (never the `smith_tests` target, which also runs the tests). Returns
-- nil and the message when the build fails or build.c declares no smith_tests.
-- `run(root, target)` builds and returns ok, output (default: the one that yields inside nvim-dap's coroutine; neotest's
-- adapter passes one that suits its own tasks).
function M.build_runner(root, run)
  run = run or build_and_wait
  local ok, output = run(root, "smith_needs")
  if not ok then return nil, "smith build failed in " .. root .. "\n" .. output end
  local runner = M.runner_of(read_info(root) or "")
  if not runner then return nil, "build.c declares no smith_tests, so there is no test runner to launch" end
  ok, output = run(root, runner)
  if not ok then return nil, "smith build failed in " .. root .. "\n" .. output end
  return absolute(root, runner)
end

-- The launch configuration as one table: the test runner with `--test <name> --in-process` when the cursor is in a
-- test (the runner forks a worker for each test and a debugger stays in the parent, so a breakpoint in the test is only
-- hit in the runner's own process), else the program `smith run` runs; the project root is the cwd.
function M.configuration()
  local root = M.project_root()
  local test = root and M.test_under_cursor()
  local configuration = { type = "smith", request = "launch", name = "smith: launch" }
  configuration.cwd = root or vim.fn.getcwd()
  if test then
    local runner, message = M.build_runner(root)
    if runner then
      configuration.program = runner
      configuration.args = { "--test", test, "--in-process" }
      return configuration
    end
    if message:find("declares no smith_tests", 1, true) then
      vim.notify("smith: " .. message, vim.log.levels.WARN) -- not a failed build: launch the program instead
    else
      configuration.program = fail(message)
      return configuration
    end
  end
  configuration.program = M.program()
  return configuration
end

-- Callable (nvim-dap calls it in its coroutine and uses the table it returns, which lets one decision set program, args
-- and cwd together); `program` and `cwd` are plain fields too, for an nvim-dap that does not call it.
local function launch()
  return setmetatable({
    type = "smith",
    request = "launch",
    name = "smith: launch",
    program = M.program,
    cwd = function() return M.project_root() or vim.fn.getcwd() end,
  }, { __call = M.configuration })
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

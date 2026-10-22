-- A neotest adapter for smith's tests (`<smith/test.h>`, `smith_tests`): C and .smith test files, run with `smith test`
-- and debugged with nvim-dap through `smith dap`. It registers nothing by itself; add it to neotest's adapters:
--   require("neotest").setup({ adapters = { require("neotest-smith") } })
--
-- How it works:
--   * A test file is a `.c` or `.smith` file in a smith project (a folder with a build file, or a main.smith or main.c) whose name is
--     `test_*` or `*_test`: cheap, by name only, the way `smith_tests` globs `tests/test_*.c`.
--   * The tests are the `test_` function definitions, found with tree-sitter (the c parser, or smith's for `.smith`). A
--     position is named as `--test` takes it: `test_x`, or `a::b::test_x` when the file has `module a::b;`.
--   * A test runs as `smith test --test <name> --verbose`. `--test` is exact when a test has that name and a substring
--     otherwise, so a `.smith` file runs as `--test <module>::`, which selects the tests of the module; a `.c` file
--     has no module to select by, so it runs the suite and the results keep the file's tests. The suite is `smith test`.
--   * Debugging builds the runner (the `runner` line of .smith/build.info; first `smith build smith_needs`, which
--     writes it) and hands nvim-dap the runner with `--test <name> --in-process`, since the runner forks a worker for
--     each test and a debugger stays in the parent.
--   * The results are read from the runner's `PASS name` and `FAIL name` lines (the runner prints no machine-readable
--     line); a failure carries the failing check's file, line and values.
local M = { name = "neotest-smith" }

function M.root(dir) return require("smith.project").root(dir) end

function M.filter_dir(name) return name ~= "build" and name ~= ".smith" and name ~= ".git" end

function M.is_test_file(file_path)
  local name = vim.fs.basename(file_path)
  local stem = name:match("^(.+)%.c$") or name:match("^(.+)%.smith$")
  if not stem or not (stem:match("^test_") or stem:match("_test$")) then return false end
  return M.root(file_path) ~= nil
end

local function read_lines(path)
  local ok, lines = pcall(vim.fn.readfile, path)
  return ok and lines or {}
end

-- The functions whose name starts with `test_`, in a .c file or a .smith one.
local query = [[
  (function_definition
    declarator: (function_declarator declarator: (identifier) @test.name (#match? @test.name "^test_")))
    @test.definition
]]

-- The position of a captured test, named as `--test` takes it.
local function build_position(file_path, source, captured_nodes)
  local name = vim.treesitter.get_node_text(captured_nodes["test.name"], source)
  return {
    type = "test",
    path = file_path,
    name = require("smith.dap").test_name(vim.split(source, "\n", { plain = true }), name),
    range = { captured_nodes["test.definition"]:range() },
  }
end

-- Parses in this editor, not in neotest's subprocess: the .smith parser is compiled and registered here (smith's
-- plugin does it on the first .smith buffer, and waiting for it here covers a file no buffer has opened yet).
function M.discover_positions(file_path)
  local nio = require("nio")
  if file_path:match("%.smith$") then
    local ensure = nio.wrap(function(callback) require("smith").ensure_parser(callback) end, 1)
    nio.scheduler()
    local ok, message = ensure()
    if not ok then
      vim.schedule(function() vim.notify("smith: " .. tostring(message), vim.log.levels.WARN) end)
      return nil
    end
  end
  local content = table.concat(read_lines(file_path), "\n")
  return require("neotest.lib").treesitter.parse_positions_from_string(file_path, content, query, {
    build_position = build_position,
  })
end

-- The name the runner prints for a `--test` name: it drops `test_` from the function's own name.
function M.shown_name(name)
  local head, tail = name:match("^(.*::)(.+)$")
  head, tail = head or "", tail or name
  if tail:sub(1, 5) == "test_" and #tail > 5 then tail = tail:sub(6) end
  return head .. tail
end

-- Builds with `smith build <target>` in a neotest task, without blocking the editor.
local function build_in_task(root, target)
  local nio = require("nio")
  local run = nio.wrap(function(folder, goal, callback)
    vim.system({ "smith", "build", goal }, { cwd = folder, text = true }, function(result)
      callback(result.code == 0, (result.stderr or "") .. (result.stdout or ""))
    end)
  end, 3)
  return run(root, target)
end

function M.build_spec(args)
  local position = args.tree:data()
  local root = M.root(position.path) or vim.uv.cwd()
  local command = { "smith", "test" }
  local filter
  if position.type == "test" then
    filter = position.name
  elseif position.type == "file" then
    local module = require("smith.dap").module_of_lines(read_lines(position.path))
    filter = module and (module .. "::") or nil -- a .c file: the whole suite, and the results keep its tests
  end
  if args.strategy == "dap" then
    if position.type ~= "test" then return nil end
    local runner, message = require("smith.dap").build_runner(root, build_in_task)
    if not runner then
      vim.schedule(function() vim.notify("smith: " .. message, vim.log.levels.ERROR) end)
      return nil
    end
    return {
      cwd = root,
      context = { debugging = true },
      strategy = {
        type = "smith",
        request = "launch",
        name = "smith: debug " .. position.name,
        program = runner,
        args = { "--test", position.name, "--in-process" },
        cwd = root,
      },
    }
  end
  if filter then vim.list_extend(command, { "--test", filter }) end
  vim.list_extend(command, { "--verbose" })
  vim.list_extend(command, args.extra_args or {})
  return { command = command, cwd = root, context = { root = root } }
end

-- Reads the runner's output into `tests`, { [shown name] = { passed = bool, file, line (1-based), message } } (a failed
-- test's file and line come from the line under its FAIL), and `bodies`, { message, frames = { function names } }.
-- `smith test` pipes the runner through `smith symbolize`, which prints a check's source line and values, then
-- `called from` and the frames, when it gets to it, and that can be after the summary: so such a body is tied to its
-- test by the test's function among its frames. When the runner is run directly the message follows the FAIL line.
function M.parse_output(text)
  local tests, bodies = {}, {}
  local current, inline, body, in_frames
  for _, line in ipairs(vim.split(text, "\n", { plain = true })) do
    line = line:gsub("\27%[[%d;]*m", "")
    local verdict, name = line:match("^(%u%u%u%u) (%S+)")
    local file, number
    if current and not current.passed and not current.file then
      file, number = line:match("^%s+check failed at (%S+):(%d+)")
      if not file then file, number = line:match("^  (%S+):(%d+)%s*$") end
    end
    if (verdict == "PASS" or verdict == "FAIL") and name then
      current, inline, body, in_frames = { passed = verdict == "PASS", message = {} }, false, nil, false
      tests[name] = current
    elseif file then
      current.file, current.line, inline = file, tonumber(number), true
    elseif line:match("^%s*$") then
      inline = false
    elseif line:match("^%s+%d+%.%d+ s%s") then
      -- a line of the "slowest:" list, which can land anywhere among a symbolized block's lines
    elseif line:match("^%s+called from") then
      inline, in_frames = false, true
    elseif in_frames and line:match("^%s+%S+:%d+%s+%S+") then
      local frame = line:match("^%s+%S+:%d+%s+(%S+)")
      if body then table.insert(body.frames, frame) end
    elseif line:match("^%s") then
      in_frames = false
      if inline then
        table.insert(current.message, vim.trim(line))
      elseif body then
        table.insert(body.message, vim.trim(line))
      elseif line:match("^%s+%d+ |") then
        body = { message = { vim.trim(line) }, frames = {} }
        in_frames = false
        table.insert(bodies, body)
      end
    else
      current, inline, body, in_frames = nil, false, nil, false -- the summary, build output: not a test's
    end
  end
  return tests, bodies
end

-- The message of a failed test: the body whose frames name its function, else what followed its FAIL line.
local function message_of(position, entry, bodies)
  for _, body in ipairs(bodies) do
    if vim.tbl_contains(body.frames, position.name) then return table.concat(body.message, "\n") end
  end
  return table.concat(entry.message, "\n")
end

local function same_file(reported, path)
  return reported == path or path:sub(-#reported - 1) == "/" .. reported
end

function M.results(spec, result, tree)
  local tests, bodies = M.parse_output(table.concat(read_lines(result.output), "\n"))
  local results = {}
  for _, node in tree:iter_nodes() do
    local position = node:data()
    if position.type == "test" then
      local entry = tests[M.shown_name(position.name)]
      if entry and entry.passed then
        results[position.id] = { status = "passed", output = result.output }
      elseif entry then
        local message = message_of(position, entry, bodies)
        local here = entry.file and same_file(entry.file, position.path)
        if message == "" and here then -- the values never came (the output is interleaved): the check as written
          message = "check failed: " .. vim.trim(read_lines(position.path)[entry.line] or "")
        elseif message == "" then
          message = "check failed"
        end
        local errors = { { message = message } }
        if here then errors[1].line = entry.line - 1 end
        results[position.id] = { status = "failed", short = message, errors = errors, output = result.output }
      elseif spec.context.debugging and result.code == 0 then
        results[position.id] = { status = "passed", output = result.output }
      else
        results[position.id] = { status = "skipped", output = result.output }
      end
    end
  end
  -- the tree's own node (a file or the suite): failed when a test of it failed, else passed
  local top = tree:data()
  if top.type ~= "test" and not results[top.id] then
    local failed, passed = false, false
    for _, entry in pairs(results) do
      failed = failed or entry.status == "failed"
      passed = passed or entry.status == "passed"
    end
    results[top.id] = { status = failed and "failed" or passed and "passed" or "skipped", output = result.output }
  end
  return results
end

return M

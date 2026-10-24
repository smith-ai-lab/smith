-- The tests of the Neovim plugin (tests/neovim.sh runs each of these in a headless Neovim of its own, after
-- `runtime plugin/smith.lua`, as a user's startup would). SMITH_NVIM_TEST names the test, SMITH_REPO is the checkout and
-- SMITH_PROJECT a scratch project with a build.c and a main.c. A test returns when it passes and calls error otherwise.
local test = os.getenv("SMITH_NVIM_TEST")
local repo = os.getenv("SMITH_REPO")
local project = os.getenv("SMITH_PROJECT")

-- Nothing may wait for a person: a launch that asks for a program's path (vim.fn.input) once hung a headless Neovim for
-- ten minutes, so asking fails the test at once, and a test that means to be asked stubs this itself. A watchdog ends a
-- test that is stuck anywhere else.
vim.fn.input = function(prompt) error("asked for input: " .. tostring(prompt), 2) end
vim.defer_fn(function()
  io.stderr:write("the test ran for five minutes\n")
  vim.cmd("cquit 1")
end, 300000)

local function check(condition, message)
  if not condition then error(message, 2) end
end

local function equal(expected, actual, what)
  if not vim.deep_equal(expected, actual) then
    error(("%s\n  expected: %s\n  actual:   %s"):format(what, vim.inspect(expected), vim.inspect(actual)), 2)
  end
end

local function wait(what, condition, timeout)
  local ok = vim.wait(timeout or 90000, condition, 25)
  check(ok, "timed out waiting for " .. what)
end

-- The parser, compiled by the plugin on first use (the tests share one data folder, so only the first compiles).
local function parser_ready()
  local done, ok, message
  require("smith").ensure_parser(function(success, text)
    done, ok, message = true, success, text
  end)
  wait("the parser", function() return done end)
  check(ok, "the parser was not built: " .. tostring(message))
  return message
end

-- The captures of the highlights query over `text`, as { [capture] = { texts } }.
local function captures(text)
  parser_ready()
  local buffer = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_buf_set_lines(buffer, 0, -1, false, vim.split(text, "\n"))
  local parser = vim.treesitter.get_parser(buffer, "smith")
  local root = parser:parse()[1]:root()
  local query = vim.treesitter.query.get("smith", "highlights")
  check(query, "there is no highlights query for smith")
  local found = {}
  for id, node in query:iter_captures(root, buffer) do
    local name = query.captures[id]
    found[name] = found[name] or {}
    table.insert(found[name], vim.treesitter.get_node_text(node, buffer))
  end
  return found, root
end

local function has(list, value)
  return vim.tbl_contains(list or {}, value)
end

local tests = {}

function tests.filetype()
  local path = vim.fn.tempname() .. ".smith"
  vim.cmd("edit " .. vim.fn.fnameescape(path))
  equal("smith", vim.bo.filetype, "the file type of a .smith file")
  vim.cmd("edit " .. vim.fn.fnameescape(project .. "/main.c"))
  equal("c", vim.bo.filetype, "a C file stays C")
end

function tests.parser()
  local path = parser_ready()
  check(path:match("%.so$") and vim.uv.fs_stat(path), "the compiled parser is at " .. tostring(path))
  local info = vim.treesitter.language.inspect("smith")
  check(info.abi_version >= 14, "the parser's ABI is " .. tostring(info.abi_version))
  local before = vim.uv.fs_stat(path).mtime.nsec
  local again = parser_ready()
  equal(path, again, "the second use finds the same parser")
  equal(before, vim.uv.fs_stat(path).mtime.nsec, "the parser is not compiled twice")
  local _, root = captures("module a::b;\nimport lib::text as tx;\nexport u32 f(void) { return c::strlen(\"x\"); }\n")
  check(not root:has_error(), "a module parses without an error node: " .. root:sexpr())
end

function tests.highlights()
  local found = captures(table.concat({
    "module game::replay;",
    "import lib::text as tx;",
    "import <string.h>;",
    "export tx::Text open(const char *path) {",
    "    return tx::make(c::strlen(path));",
    "}",
    "T max(constexpr type T, T a, T b);",
    "export int kind(constexpr type T, T v) {",
    "    if constexpr (is_integer(T) && sizeof(T) == 8) { return 1; }",
    "    return tx::make(v);",
    "}",
  }, "\n"))
  check(has(found["keyword.import"], "module"), "module is a keyword.import: " .. vim.inspect(found))
  check(has(found["keyword.import"], "import"), "import is a keyword.import")
  check(has(found["keyword.import"], "as"), "as is a keyword.import")
  check(has(found["keyword.modifier"], "export"), "export is a keyword.modifier")
  for _, name in ipairs({ "game", "replay", "lib", "text", "tx", "c" }) do
    check(has(found["module"], name), name .. " is a module: " .. vim.inspect(found["module"]))
  end
  check(#(found["punctuation.delimiter"] or {}) >= 5, "the :: of paths are punctuation.delimiter")
  check(has(found["punctuation.delimiter"], "::"), "'::' is punctuation.delimiter")
  check(has(found["function.call"], "make") and has(found["function.call"], "strlen"), "called paths are function.call")
  check(has(found["keyword.type"], "type"), "the type parameter keyword")
  check(has(found["keyword.modifier"], "constexpr"), "constexpr in a parameter list is a keyword.modifier")
  check(has(found["keyword.conditional"], "constexpr"), "if constexpr: constexpr is a keyword.conditional")
  check(has(found["function.builtin"], "is_integer"), "the type questions of if constexpr are function.builtin")
  check(has(found["type"], "Text"), "a type from a module is a type")
  -- C's own captures come through `inherits: c`.
  check(has(found["keyword.return"], "return"), "return is highlighted by the inherited C query: " .. vim.inspect(found))
  check(found["type.builtin"] ~= nil, "the C query highlights builtin types")
end

function tests.highlight_buffer()
  local path = vim.fn.tempname() .. ".smith"
  vim.fn.writefile({ "module a;", "import b::c;" }, path)
  vim.cmd("edit " .. vim.fn.fnameescape(path))
  wait("highlighting to start", function() return vim.treesitter.highlighter.active[vim.api.nvim_get_current_buf()] ~= nil end)
  vim.treesitter.get_parser(0, "smith"):parse()
  local names = {}
  for _, capture in ipairs(vim.treesitter.get_captures_at_pos(0, 0, 0)) do table.insert(names, capture.capture) end
  check(vim.tbl_contains(names, "keyword.import"), "module is highlighted in the buffer: " .. vim.inspect(names))
end

function tests.plain_c()
  local found, root = captures(table.concat({
    "int module, import, export, type;",
    "int throw_it(int new, int class, int throw) { module = new + class + throw; return type; }",
    "struct S { int import; };",
  }, "\n"))
  check(not root:has_error(), "C that uses the new words as names parses without an error node: " .. root:sexpr())
  check(not has(found["keyword.import"], "module") and not has(found["keyword.import"], "import"),
    "the words are not keywords where C uses them as names: " .. vim.inspect(found))
end

function tests.lsp_config()
  local config = vim.lsp.config.smith
  check(config, "vim.lsp.config.smith is defined")
  equal({ "smith", "lsp" }, config.cmd, "the server command")
  equal({ "c", "cpp", "objc", "objcpp", "smith" }, config.filetypes, "the file types")
  equal({ "build.c", "build.smith", "main.smith", "main.c", "compile_commands.json", ".git" }, config.root_markers, "the root markers")
  check(not vim.lsp.is_enabled("smith"), "the plugin does not enable the server")
  vim.cmd("edit " .. vim.fn.fnameescape(project .. "/main.c"))
  vim.wait(500)
  equal({}, vim.lsp.get_clients({ name = "smith" }), "no server starts until the user enables one")
end

function tests.lsp_attach()
  vim.lsp.enable("smith")
  vim.cmd("edit " .. vim.fn.fnameescape(project .. "/main.c"))
  local buffer = vim.api.nvim_get_current_buf()
  wait("smith lsp to attach", function() return #vim.lsp.get_clients({ bufnr = buffer, name = "smith" }) > 0 end)
  local client = vim.lsp.get_clients({ bufnr = buffer, name = "smith" })[1]
  equal(vim.fs.normalize(vim.uv.fs_realpath(project)), vim.fs.normalize(vim.uv.fs_realpath(client.root_dir)), "the root")
  client:stop(true)
  wait("the server to stop", function() return client:is_stopped() end, 15000)
end

-- A .smith buffer gets the same server, and its diagnostics come from the lowering and clang at the text as written.
function tests.lsp_attach_smith()
  vim.lsp.enable("smith")
  vim.cmd("edit " .. vim.fn.fnameescape(project .. "/main.smith"))
  local buffer = vim.api.nvim_get_current_buf()
  equal("smith", vim.bo[buffer].filetype, "the file type")
  wait("smith lsp to attach", function() return #vim.lsp.get_clients({ bufnr = buffer, name = "smith" }) > 0 end)
  wait("diagnostics", function() return #vim.diagnostic.get(buffer) > 0 end)
  local messages = {}
  for _, diagnostic in ipairs(vim.diagnostic.get(buffer)) do
    check(not diagnostic.message:find("compiler job", 1, true), "clangd has a compile command: " .. diagnostic.message)
    table.insert(messages, diagnostic.lnum .. ":" .. diagnostic.col .. " " .. diagnostic.message)
  end
  check(#messages == 1 and messages[1]:find("^2:40 ") and messages[1]:find("missing", 1, true),
    "the one error is clang's, on line 3: " .. vim.inspect(messages))
  local client = vim.lsp.get_clients({ bufnr = buffer, name = "smith" })[1]
  client:stop(true)
  wait("the server to stop", function() return client:is_stopped() end, 15000)
end

-- A .smith buffer in a folder with no build.c, no compile_commands.json and no .git: the server attaches (single-file
-- mode) and reads the file as `smith cc` does, so the only diagnostic is the undefined name, at its place.
function tests.lsp_attach_smith_alone()
  local alone = os.getenv("SMITH_ALONE")
  vim.lsp.enable("smith")
  vim.cmd("edit " .. vim.fn.fnameescape(alone .. "/main.smith"))
  local buffer = vim.api.nvim_get_current_buf()
  wait("smith lsp to attach", function() return #vim.lsp.get_clients({ bufnr = buffer, name = "smith" }) > 0 end)
  wait("diagnostics", function() return #vim.diagnostic.get(buffer) > 0 end)
  local messages = {}
  for _, diagnostic in ipairs(vim.diagnostic.get(buffer)) do
    check(not diagnostic.message:find("compiler job", 1, true), "clangd has a compile command: " .. diagnostic.message)
    table.insert(messages, diagnostic.lnum .. ":" .. diagnostic.col .. " " .. diagnostic.message)
  end
  check(#messages == 1 and messages[1]:find("^5:9 ") and messages[1]:find("zzz", 1, true),
    "the one error is the undefined name, on line 6: " .. vim.inspect(messages))
  local client = vim.lsp.get_clients({ bufnr = buffer, name = "smith" })[1]
  client:stop(true)
  wait("the server to stop", function() return client:is_stopped() end, 15000)
end

-- A scratch folder with `files` ({ name = text }) in it, and the file type Neovim gives `name` there.
local function filetype_of(folder, files, name)
  for file, text in pairs(files) do
    vim.fn.mkdir(vim.fs.dirname(folder .. "/" .. file), "p")
    vim.fn.writefile(vim.split(text, "\n"), folder .. "/" .. file) end
  vim.cmd("edit " .. vim.fn.fnameescape(folder .. "/" .. name))
  return vim.bo.filetype
end

function tests.header_in_project()
  local base = vim.fn.tempname()
  equal("c", filetype_of(base .. "/a", { ["build.c"] = "", ["x.h"] = "", ["x.c"] = "" }, "x.h"), "a .h beside a .c")
  equal("c", filetype_of(base .. "/b", { ["build.c"] = "", ["x.h"] = "", ["x.smith"] = "" }, "x.h"), "a .h beside a .smith")
  equal("cpp", filetype_of(base .. "/c", { ["build.c"] = "", ["x.h"] = "", ["x.cpp"] = "" }, "x.h"), "a .h beside a .cpp")
  equal("objc", filetype_of(base .. "/d", { ["build.c"] = "", ["x.h"] = "", ["x.m"] = "" }, "x.h"), "a .h beside a .m")
  equal("objcpp", filetype_of(base .. "/e", { ["build.c"] = "", ["x.h"] = "", ["x.mm"] = "" }, "x.h"), "a .h beside a .mm")
  local only_c = { ["build.c"] = "", ["lone.h"] = "", [".smith/build.info"] = "compile\ta.smith\t-O2\ncompile\tb.c\t-g\ntest\tt\n" }
  equal("c", filetype_of(base .. "/f", only_c, "lone.h"), "no sibling, the build compiles only .smith and .c")
  -- found from a folder below the project root
  vim.fn.mkdir(base .. "/f/include/deep", "p")
  vim.fn.writefile({}, base .. "/f/include/deep/y.h")
  vim.cmd("edit " .. vim.fn.fnameescape(base .. "/f/include/deep/y.h"))
  equal("c", vim.bo.filetype, "a header below the project root")
  local mixed = { ["build.c"] = "", ["lone.h"] = "", [".smith/build.info"] = "compile\ta.c\ncompile\tb.cpp\n" }
  equal("cpp", filetype_of(base .. "/g", mixed, "lone.h"), "a C++ compile leaves Neovim's rule")
  local objective = { ["build.c"] = "", ["lone.h"] = "", [".smith/build.info"] = "compile\ta.c\ncompile\tb.m\n" }
  equal("objc", filetype_of(base .. "/h", objective, "lone.h"), "C and Objective-C compiles")
  equal("cpp", filetype_of(base .. "/i", { ["build.c"] = "", ["lone.h"] = "" }, "lone.h"), "no build.info, no sibling")
end

function tests.header_outside()
  local base = vim.fn.tempname()
  equal("cpp", filetype_of(base, { ["x.h"] = "", ["x.c"] = "" }, "x.h"), "outside a smith project Neovim's rule stands")
end

-- Run with g:c_syntax_for_h set: the user's choice is Neovim's rule even beside a .cpp.
function tests.header_user_choice()
  local base = vim.fn.tempname()
  equal("c", filetype_of(base, { ["build.c"] = "", ["x.h"] = "", ["x.cpp"] = "" }, "x.h"), "g:c_syntax_for_h wins")
end

-- One launch configuration per file type, so nvim-dap asks nothing: callable, with `program` and `cwd` as plain fields
-- for an nvim-dap that does not call it.
function tests.dap_one_configuration()
  local dap = require("dap")
  for _, filetype in ipairs({ "c", "cpp", "objc", "objcpp", "smith" }) do
    local configurations = dap.configurations[filetype]
    equal(1, #configurations, "exactly one configuration for " .. filetype)
    equal("smith: launch", configurations[1].name, "its name for " .. filetype)
    equal("function", type(configurations[1].program), "its program is a function, the fallback")
    equal("function", type(configurations[1].cwd), "its cwd is a function, the fallback")
    equal("function", type(getmetatable(configurations[1]).__call), "it is callable")
  end
end

-- Calls the first configuration as nvim-dap does, in a coroutine, from `folder` with `file` open (when given, with the
-- cursor at `line`); returns the table it returned, and the notifications it made. The editor must not be frozen: a
-- timer keeps running while the build does.
local function run_configuration(folder, file, line, builds)
  vim.cmd.cd(vim.fn.fnameescape(folder))
  if file then
    vim.cmd.edit(vim.fn.fnameescape(folder .. "/" .. file))
    vim.api.nvim_win_set_cursor(0, { line or 1, 0 })
  end
  local notices = {}
  vim.notify = function(message, level) table.insert(notices, { message = message, level = level }) end
  local ticks = 0
  local timer = vim.uv.new_timer()
  timer:start(0, 20, function() ticks = ticks + 1 end)
  local result, finished
  local thread = coroutine.create(function()
    result = require("dap").configurations.c[1]()
    finished = true
  end)
  assert(coroutine.resume(thread))
  check(builds == false or not finished, "the configuration yields while the build runs")
  wait("the build", function() return finished end)
  timer:stop()
  timer:close()
  check(builds == false or ticks > 0, "the event loop kept running during the build (" .. ticks .. " ticks)")
  return result, notices
end

local function run_program(folder)
  local configuration, notices = run_configuration(folder)
  return configuration.program, notices, configuration
end

function tests.dap_build_program()
  local program = run_program(os.getenv("SMITH_NEW"))
  equal(vim.uv.fs_realpath(os.getenv("SMITH_NEW") .. "/build/debug/hello"), vim.uv.fs_realpath(program), "the program")
  check(program:sub(1, 1) == "/", "an absolute path: " .. program)
  check(vim.uv.fs_stat(program), "the program was built")
  local configuration = require("dap").configurations.c[1]
  equal(vim.uv.fs_realpath(os.getenv("SMITH_NEW")), vim.uv.fs_realpath(configuration.cwd()), "cwd is the project root")
  local _, _, called = run_program(os.getenv("SMITH_NEW"))
  equal(vim.uv.fs_realpath(os.getenv("SMITH_NEW")), vim.uv.fs_realpath(called.cwd), "the called configuration's cwd")
  equal(nil, called.args, "no arguments for the program")
  equal("smith", called.type, "its type")
  equal("launch", called.request, "its request")
end

-- The line (1-based) of the first line of the file that has `text`.
local function line_of(path, text)
  for number, line in ipairs(vim.fn.readfile(path)) do
    if line:find(text, 1, true) then return number end
  end
  error(text .. " is not in " .. path)
end

-- With the cursor inside a test function of tests/test_*.c: the runner, built, and --test <name> --in-process.
function tests.dap_test_under_cursor()
  local folder = os.getenv("SMITH_NEW")
  local test_file = vim.fn.glob(folder .. "/tests/test_*.c")
  check(test_file ~= "", "the new project has a test")
  local name = vim.fn.fnamemodify(test_file, ":t")
  local lines = vim.fn.readfile(test_file)
  local function_line, function_name
  for number, line in ipairs(lines) do
    local found = line:match("^void (test_[%w_]+)%(")
    if found then function_line, function_name = number, found break end
  end
  check(function_name, "a test function in " .. test_file)
  local configuration = run_configuration(folder, "tests/" .. name, function_line + 1)
  equal(vim.uv.fs_realpath(folder .. "/build/tests"), vim.uv.fs_realpath(configuration.program), "the runner")
  check(configuration.program:sub(1, 1) == "/", "an absolute path: " .. configuration.program)
  check(vim.uv.fs_stat(configuration.program), "the runner was built")
  equal({ "--test", function_name, "--in-process" }, configuration.args, "the test, in the runner's own process")
  equal(vim.uv.fs_realpath(folder), vim.uv.fs_realpath(configuration.cwd), "cwd is the project root")
  -- the same without a parser
  local original = vim.treesitter.get_node
  vim.treesitter.get_node = function() error("no parser") end
  equal(function_name, require("smith.dap").test_under_cursor(), "the regex finds the test without a parser")
  vim.treesitter.get_node = original
  -- outside the function: the program
  vim.api.nvim_win_set_cursor(0, { 1, 0 })
  equal(nil, require("smith.dap").test_under_cursor(), "the cursor above the test is in none")
end

-- The cursor in main.c is in no test: the program, no arguments.
function tests.dap_main_under_cursor()
  local folder = os.getenv("SMITH_NEW")
  local configuration = run_configuration(folder, "main.c", line_of(folder .. "/main.c", "main(") + 1)
  equal(vim.uv.fs_realpath(folder .. "/build/debug/hello"), vim.uv.fs_realpath(configuration.program), "the program")
  equal(nil, configuration.args, "no arguments")
end

-- A .smith buffer: the name has the module; a function that is not a test is none.
function tests.dap_test_name_in_module()
  local dap = require("smith.dap")
  local buffer = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_buf_set_lines(buffer, 0, -1, false, {
    "module test_primes;",
    "import smith::test;",
    "",
    "export void test_one(void) {",
    "    test::check_equal_i32(1, 1);",
    "}",
    "",
    "void helper(void) {",
    "}",
  })
  vim.api.nvim_set_current_buf(buffer)
  vim.bo[buffer].filetype = "smith"
  vim.api.nvim_win_set_cursor(0, { 5, 4 })
  equal("test_primes::test_one", dap.test_under_cursor(), "the module is part of the name")
  vim.api.nvim_win_set_cursor(0, { 9, 0 })
  equal(nil, dap.test_under_cursor(), "a function that is not a test")
  vim.api.nvim_buf_set_lines(buffer, 0, 1, false, { "module a::b;" })
  vim.api.nvim_win_set_cursor(0, { 5, 4 })
  equal("a::b::test_one", dap.test_under_cursor(), "a nested module")
  vim.bo[buffer].filetype = "text"
  equal(nil, dap.test_under_cursor(), "only c and smith buffers")
end

-- No project: the path is asked for (and an empty answer aborts).
function tests.dap_asks_without_project()
  local folder = vim.fn.tempname()
  vim.fn.mkdir(folder, "p")
  local asked = {}
  vim.fn.input = function(prompt, default)
    table.insert(asked, { prompt, default })
    return "/some/program"
  end
  local configuration = run_configuration(folder, nil, nil, false)
  equal("/some/program", configuration.program, "the answer is the program")
  equal(1, #asked, "asked once")
  check(asked[1][2]:find(vim.uv.fs_realpath(folder), 1, true) or asked[1][2]:find(folder, 1, true), "it starts in the folder")
  vim.fn.input = function() return "" end
  local aborted = run_configuration(folder, nil, nil, false)
  check(aborted.program == require("dap").ABORT, "an empty answer aborts")
end

function tests.dap_build_fails()
  local program, notices = run_program(os.getenv("SMITH_BROKEN"))
  check(program == require("dap").ABORT, "a failed build aborts: " .. vim.inspect(program))
  equal(1, #notices, "one notification")
  equal(vim.log.levels.ERROR, notices[1].level, "at ERROR")
  check(notices[1].message:find("error", 1, true), "it shows the build's error output: " .. notices[1].message)
end

function tests.dap_runner_of()
  local of = require("smith.dap").runner_of
  equal("build/tests", of("test\ttest\nprogram\tbuild/tests\nrunner\tbuild/tests\ndefault\tx\n"), "the runner line")
  equal("build/tests", of("runner\tbuild/tests\n"), "on the first line")
  equal(nil, of("program\tbuild/tests\ndefault\tx\n"), "a build.info without one")
end

function tests.dap_program_of()
  local of = require("smith.dap").program_of
  equal("build/debug/a", of("program\tbuild/debug/a\nprogram\tbuild/debug/b\ndefault\tbuild/debug/a\n"), "the one named")
  local none, message = of("program\tbuild/a\ndefault\tother\n")
  check(none == nil and message:find("names no program", 1, true), "none: " .. tostring(message))
  local two, many = of("program\ta\nprogram\tb\ndefault\ta\ndefault\tb\n")
  check(two == nil and many:find("names more than one program: a, b", 1, true), "two: " .. tostring(many))
end

function tests.dap_registers()
  local dap = require("dap")
  check(dap.adapters.smith, "the smith adapter is registered")
  equal("executable", dap.adapters.smith.type, "the adapter type")
  equal("smith", dap.adapters.smith.command, "the adapter command")
  equal({ "dap" }, dap.adapters.smith.args, "the adapter arguments")
  for _, filetype in ipairs({ "c", "cpp", "objc", "smith" }) do
    local configurations = dap.configurations[filetype]
    check(configurations and configurations[1], "there is a launch configuration for " .. filetype)
    equal("smith", configurations[1].type, "the configuration's adapter for " .. filetype)
    equal("launch", configurations[1].request, "the configuration's request for " .. filetype)
  end
end

function tests.dap_keeps_the_users()
  local dap = require("dap")
  equal("mine", dap.adapters.smith, "an adapter the user registered stays")
  equal({ { name = "user c" } }, dap.configurations.c, "the user's C configurations stay")
  equal("smith", dap.configurations.cpp[1].type, "a file type the user left alone gets the launch configuration")
end

function tests.dap_absent()
  check(package.loaded["dap"] == nil, "nvim-dap is not loaded by the plugin when it is not installed")
  check(not require("smith.dap").register(), "nothing registers without nvim-dap")
end

-- ---- neotest-smith (knowledge/neovim.md). neotest itself is not installed for the tests: a stand-in `nio` and
-- `neotest.lib` give the adapter what it asks of them (the parse of positions is the adapter's own query run here).

local function stub_neotest()
  package.preload["nio"] = function()
    return {
      scheduler = function() end,
      -- calls fn with a callback in the last position (argc) and waits for it, as a task would
      wrap = function(fn, argc)
        return function(...)
          local args = { ... }
          local done, results
          args[argc] = function(...)
            results = { n = select("#", ...), ... }
            done = true
          end
          fn(unpack(args, 1, argc))
          vim.wait(600000, function() return done end, 20)
          return unpack(results, 1, results.n)
        end
      end,
    }
  end
  local function tree_of(list)
    return {
      data = function() return list[1] end,
      iter_nodes = function()
        local index = 0
        return function()
          index = index + 1
          local position = list[index]
          if position then return index, { data = function() return position end } end
        end
      end,
    }
  end
  package.preload["neotest.lib"] = function()
    return {
      treesitter = {
        parse_positions_from_string = function(path, content, query, options)
          local lang = path:match("%.smith$") and "smith" or "c"
          local parser = vim.treesitter.get_string_parser(content, lang)
          local root = parser:parse()[1]:root()
          local list = { { type = "file", path = path, id = path, name = vim.fs.basename(path) } }
          local parsed = vim.treesitter.query.parse(lang, query)
          for _, match in parsed:iter_matches(root, content, 0, -1) do
            local captured = {}
            for id, nodes in pairs(match) do
              captured[parsed.captures[id]] = type(nodes) == "table" and nodes[#nodes] or nodes
            end
            local position = options.build_position(path, content, captured)
            position.id = path .. "::" .. position.name
            table.insert(list, position)
          end
          return tree_of(list)
        end,
      },
    }
  end
  return tree_of
end

local function write_project(files)
  local folder = vim.fn.tempname()
  for name, text in pairs(files) do
    vim.fn.mkdir(vim.fs.dirname(folder .. "/" .. name), "p")
    vim.fn.writefile(vim.split(text, "\n"), folder .. "/" .. name)
  end
  return folder
end

local smith_test_source = table.concat({
  "module geometry::box_test;",
  "import smith::test;",
  "",
  "export void test_area(void) {",
  "    test::check_equal_i32(1, 1);",
  "}",
  "",
  "void helper(void) {",
  "}",
  "",
  "export void test_perimeter(void) {",
  "}",
}, "\n")

-- A project from `smith new` has no build file: its main.smith is what marks it, for the launch configuration and for
-- neotest.
function tests.dap_default_project()
  stub_neotest()
  local fresh = os.getenv("SMITH_FRESH")
  local program = run_program(fresh)
  equal(vim.uv.fs_realpath(fresh .. "/build/debug/fresh"), vim.uv.fs_realpath(program), "the program")
  local root = require("smith.project").root
  equal(vim.uv.fs_realpath(fresh), vim.uv.fs_realpath(root(fresh .. "/tests")), "main.smith marks the root")
  check(require("neotest-smith").is_test_file(fresh .. "/tests/test_primes.smith"), "a test file, with no build file")
  equal(nil, root(write_project({ ["test_x.c"] = "" })), "no root without a build file or a main")
  local with_main = write_project({ ["main.c"] = "" })
  equal(vim.uv.fs_realpath(with_main), vim.uv.fs_realpath(root(with_main)), "main.c marks the root")
end

function tests.neotest_is_test_file()
  local adapter = require("neotest-smith")
  local folder = os.getenv("SMITH_NEW")
  local test_file = vim.fn.glob(folder .. "/tests/test_*.c")
  check(adapter.is_test_file(test_file), "tests/test_*.c of a project")
  check(not adapter.is_test_file(folder .. "/main.c"), "main.c is not a test file")
  check(not adapter.is_test_file(folder .. "/build.c"), "build.c is not a test file")
  local other = write_project({ ["build.c"] = "", ["box_test.smith"] = "", ["box.smith"] = "", ["test_box.h"] = "" })
  check(adapter.is_test_file(other .. "/box_test.smith"), "*_test.smith")
  check(not adapter.is_test_file(other .. "/box.smith"), "box.smith")
  check(not adapter.is_test_file(other .. "/test_box.h"), "only .c and .smith")
  local alone = write_project({ ["test_x.c"] = "" })
  check(not adapter.is_test_file(alone .. "/test_x.c"), "outside a smith project")
  equal(vim.uv.fs_realpath(folder), vim.uv.fs_realpath(adapter.root(folder .. "/tests")), "the root")
  equal(nil, adapter.root(alone), "no root outside a project")
  check(not adapter.filter_dir("build") and adapter.filter_dir("tests"), "build/ is left out")
end

function tests.neotest_shown_name()
  local shown = require("neotest-smith").shown_name
  equal("area", shown("test_area"), "a C test")
  equal("geometry::box_test::area", shown("geometry::box_test::test_area"), "a module's test")
  equal("test_", shown("test_"), "a name that is only the prefix stays")
end

function tests.neotest_discover_c()
  stub_neotest()
  local folder = os.getenv("SMITH_NEW")
  local path = vim.fn.glob(folder .. "/tests/test_*.c")
  local expected = {}
  for _, line in ipairs(vim.fn.readfile(path)) do
    local found = line:match("^void (test_[%w_]+)%(")
    if found then table.insert(expected, found) end
  end
  check(#expected > 0, "the new project has tests")
  local found = {}
  for _, node in require("neotest-smith").discover_positions(path):iter_nodes() do
    local position = node:data()
    if position.type == "test" then table.insert(found, position.name) end
  end
  equal(expected, found, "the test functions, named as --test takes them")
end

function tests.neotest_discover_smith()
  stub_neotest()
  parser_ready()
  local folder = write_project({ ["build.c"] = "", ["box_test.smith"] = smith_test_source })
  local found = {}
  for _, node in require("neotest-smith").discover_positions(folder .. "/box_test.smith"):iter_nodes() do
    local position = node:data()
    if position.type == "test" then
      table.insert(found, { position.name, position.range[1] })
    end
  end
  equal({ { "geometry::box_test::test_area", 3 }, { "geometry::box_test::test_perimeter", 10 } }, found,
    "the tests, with the module in the name, and not the helper")
end

local function spec_for(position, strategy)
  local adapter = require("neotest-smith")
  return adapter.build_spec({
    tree = { data = function() return position end },
    strategy = strategy or "integrated",
  })
end

function tests.neotest_build_spec()
  stub_neotest()
  local folder = os.getenv("SMITH_NEW")
  local root = vim.uv.fs_realpath(folder)
  local c_file = vim.fn.glob(folder .. "/tests/test_*.c")
  local test = { type = "test", name = "test_x", path = c_file, id = c_file .. "::test_x" }
  local spec = spec_for(test)
  equal({ "smith", "test", "--test", "test_x", "--verbose" }, spec.command, "a test")
  equal(folder, spec.cwd, "cwd is the root")
  equal({ "smith", "test", "--verbose" }, spec_for({ type = "file", name = "t", path = c_file, id = c_file }).command,
    "a .c file runs the suite")
  local module_file = write_project({ ["build.c"] = "", ["box_test.smith"] = smith_test_source })
  equal({ "smith", "test", "--test", "geometry::box_test::", "--verbose" },
    spec_for({ type = "file", name = "t", path = module_file .. "/box_test.smith", id = "x" }).command,
    "a .smith file selects its module")
  equal({ "smith", "test", "--verbose" }, spec_for({ type = "dir", name = "p", path = folder, id = folder }).command,
    "the suite")
  local debug_spec = spec_for(test, "dap")
  local strategy = debug_spec.strategy
  equal("smith", strategy.type, "the dap adapter")
  equal("launch", strategy.request, "the request")
  equal(root .. "/build/tests", vim.uv.fs_realpath(strategy.program), "the runner, built")
  check(strategy.program:sub(1, 1) == "/", "an absolute path")
  check(vim.uv.fs_stat(strategy.program), "the runner exists")
  equal({ "--test", "test_x", "--in-process" }, strategy.args, "the test, in the runner's own process")
  equal(folder, strategy.cwd, "the project root")
  equal(nil, spec_for({ type = "file", name = "t", path = c_file, id = c_file }, "dap"), "only a test is debugged")
end

-- Real output of the runner (`smith test --verbose`) for one passing and one failing test: with the check's block
-- right under FAIL (the runner run directly), and as `smith test` prints it, after the summary.
local direct_output = [[
PASS test_primes::the_first_five_primes_add_up_to_28
FAIL test_primes::no_primes_add_up_to_nothing
  tests/test_primes.smith:12
      test::check_equal(primes::sum(0), 1);
    primes::sum(0) = 0, 1   (i32)

slowest:
      0.00 s  test_primes::the_first_five_primes_add_up_to_28

1 passed, 1 failed in 0.01 s
]]
local piped_output = [[
[15/15] .smith/program --run fffffffffffe5c60 --input build/tests --arg build/tests --arg --verbose
<ESC>[32mPASS<ESC>[0m test_primes::the_first_five_primes_add_up_to_28
<ESC>[31mFAIL<ESC>[0m test_primes::no_primes_add_up_to_nothing
  check failed at tests/test_primes.smith:12

slowest:
      0.00 s  test_primes::the_first_five_primes_add_up_to_28

1 passed, 1 failed in 0.01 s
       12 |     test::check_equal(primes::sum(0), 1);
            primes::sum(0) = 0, 1   (i32)
  called from
    tests/test_primes.smith:12     test_primes::test_no_primes_add_up_to_nothing
    .smith/lib/smith/test.smith:1065   smith_test_execute
smith: test: failed (status 1)
]]
piped_output = piped_output:gsub("<ESC>", "\27")

local function results_of(output, context, code)
  local tree_of = stub_neotest()
  local path = vim.fn.tempname()
  vim.fn.writefile(vim.split(output, "\n"), path)
  local file = "/p/tests/test_primes.smith"
  local list = {
    { type = "file", id = file, path = file, name = "test_primes.smith" },
    { type = "test", id = file .. "::a", path = file, name = "test_primes::test_the_first_five_primes_add_up_to_28" },
    { type = "test", id = file .. "::b", path = file, name = "test_primes::test_no_primes_add_up_to_nothing" },
    { type = "test", id = file .. "::c", path = file, name = "test_primes::test_never_ran" },
  }
  return require("neotest-smith").results({ context = context or {} }, { output = path, code = code or 1 }, tree_of(list)),
    file
end

function tests.neotest_results()
  for _, output in ipairs({ direct_output, piped_output }) do
    local results, file = results_of(output)
    equal("passed", results[file .. "::a"].status, "the passing test")
    local failed = results[file .. "::b"]
    equal("failed", failed.status, "the failing test")
    equal(11, failed.errors[1].line, "at the failing check's line, 0-based")
    check(failed.errors[1].message:find("primes::sum(0) = 0, 1", 1, true), "with the values: " .. failed.errors[1].message)
    equal("skipped", results[file .. "::c"].status, "a test that did not run")
    equal("failed", results[file].status, "the file failed when a test did")
  end
  -- only the line under FAIL, the values lost: the failing check's line, and the check as written in the file
  local file = vim.fn.tempname() .. "/tests/test_primes.smith"
  vim.fn.mkdir(vim.fs.dirname(file), "p")
  vim.fn.writefile({ "module test_primes;", "", "export void test_x(void) {", "    c::check(1, 2);", "}" }, file)
  local tree_of = stub_neotest()
  local output = vim.fn.tempname()
  vim.fn.writefile({ "FAIL test_primes::x", "  check failed at tests/test_primes.smith:4", "", "1 passed, 1 failed" }, output)
  local lost = require("neotest-smith").results({ context = {} }, { output = output, code = 1 }, tree_of({
    { type = "file", id = file, path = file, name = "f" },
    { type = "test", id = file .. "::x", path = file, name = "test_primes::test_x" },
  }))[file .. "::x"]
  equal(3, lost.errors[1].line, "the line is known")
  equal("check failed: c::check(1, 2);", lost.errors[1].message, "the check as written")
  local debugged = results_of("", { debugging = true }, 0)
  equal("passed", debugged["/p/tests/test_primes.smith::a"].status, "a debugged test that printed nothing and exited 0")
end

local function version_check()
  local notices = {}
  vim.notify = function(message) table.insert(notices, message) end
  local state
  require("smith").check_version(function(result) state = result end)
  wait("the version check", function() return state end, 15000)
  vim.wait(100)
  return state, notices
end

function tests.version_same()
  local state, notices = version_check()
  equal("same", state, "the plugin and smith are one commit")
  equal({}, notices, "no warning")
end

function tests.version_different()
  local state, notices = version_check()
  equal("different", state, "the plugin and smith differ")
  equal(1, #notices, "one warning")
  check(notices[1]:find("0123456", 1, true), "the warning names smith's commit: " .. notices[1])
  local again = {}
  require("smith").check_version(function(result) table.insert(again, result) end)
  wait("the second check", function() return #again > 0 end, 15000)
  vim.wait(100)
  equal(1, #notices, "the warning is shown once")
end

function tests.version_at_startup()
  wait("the warning at startup", function() return #_G.smith_notices > 0 end, 15000)
  equal(1, #_G.smith_notices, "one warning at startup")
  check(_G.smith_notices[1]:find("0123456", 1, true), "it names smith's commit: " .. _G.smith_notices[1])
end

function tests.version_unknown()
  local state, notices = version_check()
  equal("smith has no commit", state, "a smith built without a commit is not compared")
  equal({}, notices, "no warning")
end

function tests.no_smith()
  local state, notices = version_check()
  equal("no smith", state, "nothing to compare without smith")
  equal({}, notices, "no warning")
  local done, ok, message
  require("smith").ensure_parser(function(success, text) done, ok, message = true, success, text end)
  wait("the answer", function() return done end, 5000)
  check(not ok and message:find("not on the PATH", 1, true), "the parser cannot be built without smith: " .. tostring(message))
end

check(tests[test], "no test called " .. tostring(test))
local ok, err = pcall(tests[test])
if ok then
  io.stdout:write("ok    " .. test .. "\n")
  vim.cmd("qall!")
else
  io.stdout:write("FAIL  " .. test .. ": " .. tostring(err) .. "\n")
  vim.cmd("cquit 1")
end

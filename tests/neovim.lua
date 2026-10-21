-- The tests of the Neovim plugin (tests/neovim.sh runs each of these in a headless Neovim of its own, after
-- `runtime plugin/smith.lua`, as a user's startup would). SMITH_NVIM_TEST names the test, SMITH_REPO is the checkout and
-- SMITH_PROJECT a scratch project with a build.c and a main.c. A test returns when it passes and calls error otherwise.
local test = os.getenv("SMITH_NVIM_TEST")
local repo = os.getenv("SMITH_REPO")
local project = os.getenv("SMITH_PROJECT")

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
  equal({ "build.c", "build.smith", "compile_commands.json", ".git" }, config.root_markers, "the root markers")
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
  check(#messages == 1 and messages[1]:find("^0:44 ") and messages[1]:find("missing", 1, true),
    "the one error is clang's, on line 1: " .. vim.inspect(messages))
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
  check(#messages == 1 and messages[1]:find("^4:9 ") and messages[1]:find("zzz", 1, true),
    "the one error is the undefined name, on line 5: " .. vim.inspect(messages))
  local client = vim.lsp.get_clients({ bufnr = buffer, name = "smith" })[1]
  client:stop(true)
  wait("the server to stop", function() return client:is_stopped() end, 15000)
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

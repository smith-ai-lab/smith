-- The file type of a `.h` file in a smith project. Neovim reads every `.h` as C++ (unless it sees `@interface`, or the
-- user set g:c_syntax_for_h), which is wrong in a C project, and highlighting and ftplugins follow the file type. Only
-- inside a smith project (a folder at or above the file with a build file, a main.smith or main.c, or .smith/) does this decide anything;
-- everywhere else, and whenever it has no answer, the extension function returns nil and Neovim's own rule runs.
--   1. g:c_syntax_for_h or g:ch_syntax_for_h set: the user's choice, which is Neovim's rule.
--   2. a source beside the header with the same stem: .c or .smith is c, .cpp .cc .cxx cpp, .m objc, .mm objcpp.
--   3. else the compiles in .smith/build.info (`compile<TAB>source<TAB>flag...`): all C is c, all C or Objective-C objc.
--   4. else Neovim's rule.
local M = {}

M.siblings = {
  { "c", "c" },
  { "smith", "c" },
  { "cpp", "cpp" },
  { "cc", "cpp" },
  { "cxx", "cpp" },
  { "m", "objc" },
  { "mm", "objcpp" },
}

-- The file type that the compiles of a build.info text agree on, or nil.
function M.from_compiles(text)
  local seen_c, seen_objc = false, false
  for line in text:gmatch("[^\n]+") do
    local kind, source = line:match("^([^\t]+)\t([^\t]+)")
    if kind == "compile" then
      local extension = source:match("%.(%w+)$")
      if extension == "c" or extension == "smith" then
        seen_c = true
      elseif extension == "m" then
        seen_objc = true
      else
        return nil
      end
    end
  end
  if seen_objc then return "objc" end
  if seen_c then return "c" end
  return nil
end

-- The answer for the header at `path`, or nil for "Neovim's rule".
function M.detect(path)
  if vim.g.c_syntax_for_h or vim.g.ch_syntax_for_h then return nil end
  path = vim.fs.normalize(path)
  local root = require("smith.project").root(path, { ".smith" })
  if not root then return nil end
  local stem = path:gsub("%.h$", "")
  for _, pair in ipairs(M.siblings) do
    if vim.uv.fs_stat(stem .. "." .. pair[1]) then return pair[2] end
  end
  local file = io.open(root .. "/.smith/build.info", "rb")
  if not file then return nil end
  local text = file:read("a")
  file:close()
  return M.from_compiles(text)
end

-- Our function replaces Neovim's for "h" (a nil from it does not fall back), so step 4 calls Neovim's own.
function M.register()
  local function header(path, buffer) return M.detect(path) or require("vim.filetype.detect").header(path, buffer) end
  vim.filetype.add({ extension = { h = header } })
end

return M

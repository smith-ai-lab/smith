-- Where a smith project is, by the rule smith itself uses: a folder with a build file (build.c or build.smith), else a
-- folder with a `main` at its root (main.smith or main.c), which smith builds by convention with no build file. The
-- launch configuration, the neotest adapter and the `.h` file type rule all ask here, so they agree.
local M = {}

-- One list, so the nearest folder holding any of them is the project's.
M.markers = { "build.c", "build.smith", "main.smith", "main.c" }

-- The root of the project that `path` (a file or folder) is in, or nil. `extra` markers join the list.
function M.root(path, extra)
  local markers = M.markers
  if extra then markers = vim.list_extend(vim.list_slice(markers), extra) end
  return vim.fs.root(path, markers)
end

return M

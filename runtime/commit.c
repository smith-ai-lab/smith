// The commit smith was built from, which `smith version` prints. A translation unit of its own, so that a new commit
// recompiles this file and nothing else: `smith build` passes -DSMITH_COMMIT (none outside a checkout).
#ifndef SMITH_COMMIT
#define SMITH_COMMIT ""
#endif

const char smith_commit[] = SMITH_COMMIT;

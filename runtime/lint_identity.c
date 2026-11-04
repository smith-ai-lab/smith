// The identity of smith's lint (#2236): the LLVM pack and the sources of smith's own clang-tidy checks, hashed by
// `smith build` (build_lint_identity). `smith lint` keys its cache on it instead of the whole binary, so a change that
// cannot move a finding leaves the cached results. A translation unit of its own, so a new identity recompiles this
// file and nothing else. Empty when the build has no LLVM install: the cache then keys on the binary.
#ifndef SMITH_LINT_IDENTITY
#define SMITH_LINT_IDENTITY ""
#endif

const char smith_lint_identity[] = SMITH_LINT_IDENTITY;

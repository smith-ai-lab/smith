// The identities of smith's lint (#2236) and fmt (#3081), hashed by `smith build` (build_lint_identity and
// build_fmt_identity). The lint's is the LLVM pack and the sources of smith's own clang-tidy checks; the fmt's is the
// pack and what formats (front/, the runtime, smith's default style). `smith lint` and `smith fmt` key their caches on
// them instead of the whole binary, so a change that cannot move a finding or a format leaves the cached results. A
// translation unit of its own, so a new identity recompiles this file and nothing else. Empty when the build has no
// LLVM install: the caches then key on the binary.
#ifndef SMITH_LINT_IDENTITY
#define SMITH_LINT_IDENTITY ""
#endif

const char smith_lint_identity[] = SMITH_LINT_IDENTITY;

#ifndef SMITH_FMT_IDENTITY
#define SMITH_FMT_IDENTITY ""
#endif

const char smith_fmt_identity[] = SMITH_FMT_IDENTITY;

// The identities of a compile (#3175), what the step cache (runtime/executor_cache.c) keys the compiler of a smith-cc step
// by in place of the whole binary: the embedded clang and the sources that run inside a C, C++ or Objective-C compile
// (`build_compile_identity`), and the same with the front end for a .smith compile (`build_lang_identity`). Empty
// without an LLVM install, and the step cache then keys on the binary.
#ifndef SMITH_COMPILE_IDENTITY
#define SMITH_COMPILE_IDENTITY ""
#endif

const char smith_compile_identity[] = SMITH_COMPILE_IDENTITY;

#ifndef SMITH_LANG_IDENTITY
#define SMITH_LANG_IDENTITY ""
#endif

const char smith_lang_identity[] = SMITH_LANG_IDENTITY;

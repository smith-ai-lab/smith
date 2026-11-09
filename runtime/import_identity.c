// The identity of the new front end's imports (#2891): every input of what runtime/import_cache.c stores, hashed by
// `smith build` (build_import_identity): the files under front/ and lib/std/, shim/cimport*, the runtime's sources,
// build.c, the LLVM pack's identity, the compile flags and the cache's format version. The cache's folder is named by
// it, so a smith built from other sources reads none of another's entries. A translation unit of its own, so a new
// identity recompiles this file and nothing else. Empty when the build has no LLVM install: the cache is then off.
#ifndef SMITH_IMPORT_IDENTITY
#define SMITH_IMPORT_IDENTITY ""
#endif

const char smith_import_identity[] = SMITH_IMPORT_IDENTITY;

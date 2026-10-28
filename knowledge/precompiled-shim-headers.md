---
type: design
title: The shims that read clang's AST are compiled with a precompiled header
description: shim/pch.h holds the clang and LLVM headers four shims include, build.c compiles it once to build/<OS>-<arch>/shim.pch as a step of the graph, and those shims are compiled with it, which saves about a third of their compile.
tags: [build, llvm, speed]
---
# The shims that read clang's AST are compiled with a precompiled header

Measured in #639: `hook`, `tidy`, `affine` and `effects_infer` (`shim/*.cpp`) are the shims whose compile is much of clang's and LLVM's headers: their includes alone took 1.9 to 2.8 s of 6 to 13 s. A plain precompiled header saved a few tenths of a second; with `-fpch-instantiate-templates` (the templates' instantiations are made once, in the header) `hook.cpp` went from 5.9 to 3.7 s CPU, `affine.cpp` from 8.8 to 6.0 s, `effects_infer.cpp` from 7.1 to 4.2 s and `tidy.cpp` from 13.2 to 11.2 s; the header itself costs 6.5 s once. `lsp` and `clangd` got slower (2.3 to 3.1 s, 0.8 to 1.2 s: loading a 180 MB header costs more than their includes), so they stay out.

- `shim/pch.h` holds only system headers (clang's, LLVM's, the standard library's); a header of smith's own would make every shim depend on it. The shims keep their own `#include`s, which the guards make free.
- `build_shim_pch` in `build.c` compiles it as one more object (`<folder>/shim.pch`, in `build/<OS>-<arch>/`, so it goes with `smith build clean` and is no cache to cap) with the shims' own flags (`build_shim_flags`, the one list both use: a precompiled header must be made with the flags of the files that use it) plus `-fpch-instantiate-templates -x c++-header`. As any object it depends on the compiler's file, its command holds the compiler's and LLVM's paths (the LLVM name, so a new LLVM or flags make it again; an LLVM is never edited in place) and its depfile lists `shim/pch.h` only, since the LLVM headers are system headers. Checked with `-d explain`: a change to `shim/pch.h` makes it again and then the four shims. It starts at once as the graph's first step, in parallel with every other compile.
- `build_shim` gives the four shims `-include-pch <that file>` and the file as an implicit input, so the graph makes it first and a change to it compiles them again. The other shims are small (0.4 to 2.3 s), and loading the header would cost them more than it saves; the prover's shims (C++20, RTTI) have other flags and would need a header of their own.
- A new shim that includes many clang headers is added to `build_shim_uses_pch`; a header it needs that the others do not is added to `shim/pch.h`.

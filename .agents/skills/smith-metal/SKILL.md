---
name: smith-metal
description: "Metal shaders with smith: compiling .metal files to .air and a .metallib in build.c, smith cc on a .metal input, SMITH_METAL, '.metal and other inputs need different compilers', 'cannot find xcrun'. Load when a project has .metal files, a metallib, shaders, or a GPU kernel to build, or on a metal compile error."
---

# Metal shaders

This skill ships in the smith plugin, from `.agents/skills/smith-metal/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

smith does not compile Metal itself: a command whose inputs are all `.metal` is handed to Apple's compiler (`xcrun -sdk macosx metal`, or `$SMITH_METAL`), with the arguments unchanged. `smith cc --smith-which -c k.metal` prints `metal /usr/bin/xcrun -sdk macosx metal`. `.metal` mixed with other inputs in one command is refused (`.metal and other inputs need different compilers`): compile shaders in their own command with their own flags. It needs the Xcode command line tools (`cannot find xcrun` otherwise). A response file (`@file`) is opaque, so a `.metal` input inside one goes to clang.

## In build.c

Two steps, a `.air` per shader and a `.metallib` from the `.air` files (tried; `smith build` printed both commands and made `build/shaders.metallib`):

```c
SmithStep air = {
    .command = (const char *[]){"smith-cc", "-std=metal3.1", "-c", "$in", "-o", "$out", nullptr},
    .inputs = (const char *[]){"k.metal", nullptr},
    .outputs = (const char *[]){"build/k.air", nullptr},
};
smith_step(b, &air);
SmithStep lib = {
    .command = (const char *[]){"xcrun", "-sdk", "macosx", "metallib", "$in", "-o", "$out", nullptr},
    .inputs = air.outputs,                       // one .air per shader file; $in is all of them
    .outputs = (const char *[]){"build/shaders.metallib", nullptr},
};
smith_step(b, &lib);
```

- `#include "common.h"` in a shader: add a `.depfile` (`-MMD -MF build/k.d`) to the first step, or list the header in `.implicit_inputs`, so editing it rebuilds the shader.
- A program or test that loads the metallib at run time lists it as an input: `smith_tests`' `.inputs = smith_list(b, "build/shaders.metallib", nullptr)` builds it first and reruns the tests when it changes (`smith-test`).
- Link a program that uses Metal with `.libraries = {"-framework", "Metal", "-framework", "Foundation", nullptr}` (`smith_program`, or `smith_app`'s `.libraries`). Objective-C files that use it are `.m` and compile with `smith-cc` like C.
- `smith fmt` formats a `.metal` file as C++ (clang-format's default for an unknown extension). `smith_modules` never applies to `.metal` or C++.
- Apple's `metal` output is not reproducible: two identical builds of a metallib differ in bytes, so compare shaders by the command smith runs (`smith build -t commands build/shaders.metallib`), not by bytes.

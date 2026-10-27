---
name: smith-build-objects
description: "smith_object and smith_program in build.c: one compile and one link by hand, depfiles, .smith sources as objects, and the compile database the editor reads. Load when writing an object or a program step by hand, or when smith lsp misses a compile command."
---

# smith_object and smith_program

## High level

- `smith_object(b, &object)` is one compile (`<compiler> <flags> -MMD -MF <output>.d -c <source> -o <output>`, with that depfile), `smith_program(b, &program)` links objects (`<linker> <flags> <objects> <libraries> -o <output>`), and `smith_compile_commands(b, path)` writes compile_commands.json from them, plus an entry for build.c. They emit ordinary steps; `smith build -v` shows the commands. `smith_compile_command(b, compiler, source, flags)` adds an entry with no step for a file the build never compiles on its own (a unity-build source), so the editor knows its flags; call it before `smith_compile_commands`, and a file that already has an entry is skipped. Pass each object to `smith_object` before the program that links it. On macOS `smith_program` also links the handlers runtime (`knowledge/handlers.md`): a step makes `libsmith_handlers.dylib` beside the program (`build/<profile>/`), and the link has `-lsmith_handlers` and the rpath `@executable_path`, so the dylib travels with the program; with ASan the sanitizer's runtime comes first. A program named `smith` is the one exception. Libraries that are files rebuild the program; `-framework X` is not a file. A change of compiler rebuilds every object and program it made, and a build with the same compiler does nothing: each depends on `.smith/compiler/<hash>`, which names the compiler (smith is the clang it runs: for the clang inside it the LLVM version and a hash of what is linked, so a smith rebuilt with the same LLVM rebuilds nothing; for `SMITH_CLANG`, that clang's file).
- `.bootstrap = true` on an object is for stage 1 of a staged build (smith's own, `knowledge/smith-bootstrap.md`): a compile that fails prints `smith: stage 1 leaves <source> out` and leaves an empty object, and a program with `.lenient = true` links without the symbols no object defines (they fail only when called). Both default off, and a failed compile then fails the build.
- A `.smith` source's entry has `-x c -std=gnu2y` (the standard `smith cc` gives it; your own `-std=` flag replaces it) after the compiler and `-I .smith/include`, so `smith lsp` can read it (see `smith-check`, ".smith files").
- clangd reads it as it is: use `smith lsp` (clangd, from the LLVM smith compiles with; see the smith-check skill for editor setup) with no `--query-driver`. With `--query-driver` clangd cannot build the SDK's modules, and without it the compile command naming `smith-cc` works, module flags included. lldb stays Apple's.

```c
void build(SmithBuild *b) {
    SmithObject main = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o",
                        .flags = (const char *[]){"-std=gnu23", "-g", nullptr}};
    smith_object(b, &main);
    SmithProgram app = {.linker = "smith-cc", .output = "build/app",
                        .objects = (const SmithObject *[]){&main, nullptr},
                        .libraries = (const char *[]){"-lm", nullptr}};
    smith_program(b, &app);
    smith_compile_commands(b, "compile_commands.json"); // last
    smith_default(b, app.step.outputs);
}
```

Also in this family: [smith-build](../smith-build/SKILL.md), [smith-build-smith](../smith-build-smith/SKILL.md), [smith-build-c](../smith-build-c/SKILL.md), [smith-build-functions](../smith-build-functions/SKILL.md), [smith-build-makefile](../smith-build-makefile/SKILL.md).

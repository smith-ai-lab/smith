---
name: smith-io-capabilities
description: "Passing smith::io's capabilities down: a function takes the part of io::Io it needs (io::Console, io::Files, io::Clock, io::Processes, io::Net, io::Args), so its signature says what it can do. Load when writing a function that prints, reads files or runs processes, or when smith-effects asks for a capability parameter."
---

# Pass only what is needed

main hands each function the narrowest thing that covers what it uses, so a function that takes no clock cannot depend on time and one that takes no files cannot touch the disk.

```smith
import smith::arena;
import smith::io;

typedef struct {
    usize vertices;
} Mesh;

// One or two capabilities: those alone.
Mesh mesh_load(io::Files *files, arena::Arena *a, const char *path) {
    io::Read text = io::files_read(files, a, path);
    return (Mesh){.vertices = text.error.code == 0 ? countof(text.data) : 0};
}

// Most of io: the whole of it.
void game_run(io::Io *io) {
    (void)io::console_printf(&io->console, "running\n");
}

// A bundle several functions share: a struct the project defines, with what that group needs.
typedef struct {
    io::Files *files;
    io::Console *console;
    arena::Arena *arena;
} Editor;

void editor_open(Editor *editor, const char *path) {
    Mesh mesh = mesh_load(editor->files, editor->arena, path);
    (void)io::console_printf(editor->console, "%zu\n", mesh.vertices);
}

i32 main(i32 argc, char **argv) {
    io::Io io = io::make(argc, argv);
    Mesh mesh = mesh_load(&io.files, &io.arena, "cube.obj");
    Editor editor = {&io.files, &io.console, &io.arena};
    editor_open(&editor, "cube.obj");
    game_run(&io);
    return mesh.vertices == 0 ? 0 : 1;
}
```

- One or two capabilities: those alone (`mesh_load`).
- Most of io: the whole of it (`game_run`).
- A bundle several functions share: a struct the project defines (`Editor`).

If a signature wants four or more separate capabilities, pass `io::Io *` or define the struct. A random key is passed alone (`io.key`; derive one per use, see smith-random).

Also in this family: [smith-io](../smith-io/SKILL.md), [smith-effects](../smith-effects/SKILL.md), [smith-io-errors](../smith-io-errors/SKILL.md), [smith-io-files](../smith-io-files/SKILL.md), [smith-io-processes](../smith-io-processes/SKILL.md), [smith-io-async](../smith-io-async/SKILL.md), [smith-io-net](../smith-io-net/SKILL.md), [smith-io-fake](../smith-io-fake/SKILL.md), [smith-io-c](../smith-io-c/SKILL.md).

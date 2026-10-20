# smith build: the 23 cases

Each case is one small build problem, written in four systems: GNU Make, ninja, `build.zig` and a smith `build.c`. They
were written so the owner can choose the API of `smith build`; the choice is in [build-c.md](build-c.md). The order is
the build-shootout cases (basic to system2, 18 of them), then smith's own: variants, tool, options, function, platform.

## How they were run

- Tools: GNU Make 3.81, ninja 1.13.2, Zig 0.17.0 (Homebrew), on macOS. The compiler is Apple clang 21 through smith; the
  `tool` case uses Homebrew clang 23.1.3, because `#embed` needs it (`zig cc` is clang 22.1.8).
- make and ninja: each case was run in a scratch folder. Build, change what the case changes, build again, and read what
  reran. Helper scripts such as `gen.sh` and `run.sh` append a line to a log, so "what reran" is read from the log. Make
  3.81 compares timestamps in whole seconds, so every change was separated from the last build by a sleep of more than a
  second.
- build.zig: written first from the Zig 0.15 documentation, then run on Zig 0.17.0 in a scratch folder and corrected
  where it failed. The snippets below are the corrected ones.
- smith build.c: it did not run when the cases were written, because the API did not exist. The ninja under **ninja** is
  the `build.ninja` that the `build.c` would generate, written by hand and run. Where a decision of v1 changed what
  smith generates (task edges, stamps, `.smith/` paths), the block was updated and not rerun; a sentence under it
  says so. That is the test of whether the design works on ninja. Each
  smith heading names the API tier the snippet needs (v1, v1.5 or later); the tiers are in [build-c.md](build-c.md).
- In the ninja snippets one rule is written per kind of step, to keep them short. smith would emit one generic `rule
  step { command = $cmd }` and put the fully quoted command on each edge as `cmd = ...`, so each step's command is
  explicit and a change to it reruns the step. The two are equivalent.

## What the smith snippets assume

- Lists are C23 compound literals ending in `nullptr`.
- `smith_step` and `smith_task` take the step by pointer and update it in place. The step is declared first, because
  clang-format indents a call that holds the whole `(SmithStep){...}` very deeply.
- Every `build.c` calls `smith_default`. The ninja blocks do not always show the `default` line that smith would write.
- The build program runs on every `smith build`, so a read through `smith_glob`, `smith_env`, `smith_option_value`,
  `smith_capture` or `smith_lines` is always current.
- In `command`, only the whole words `$in` and `$out` expand. In the hand-written ninja blocks ninja expands and
  shell-quotes them (verified in `spaces`). smith's generated manifest writes the paths directly instead, each
  shell-quoted like a literal word: ninja evaluates an edge variable such as `cmd` before the edge's `$in` and `$out`
  exist. smith writes every other `$` as `$$`, so a shell script in a command keeps its `$1` and `$(...)`.
- Literal words that contain spaces or shell characters are single-quoted by smith, and paths in `build` lines are
  escaped.
- `b->os` is `uname -s` exactly ("Darwin", "Linux") and `b->arch` is `uname -m` exactly.

## Zig 0.17.0 facts that recur

- `zig build` caches the result of running `build.zig` (the configure phase), so `build.zig` is not rerun on every
  invocation. Anything it reads from the file system or the environment must be declared (`b.dependOnFileContents`,
  `b.dependOnDirectoryContents`) or the cache poisoned (`b.graph.poisonCache()`). Otherwise a change is missed. This was
  measured for a new file (`wildcard`), a changed list (`monad1`) and a changed environment variable (`system2`).
- Run-step outputs go to `.zig-cache/o/<hash>/` and are copied by the install step to `zig-out/`.
- File-system calls take an `io` (`b.graph.io`), and the project folder is `b.root.root_dir.handle`. `b.build_root` and
  `std.process.getEnvVarOwned` no longer exist.
- A file given to a Run step as an argument is compared by size, inode and modification time. A file named in a depfile
  is compared by content. A Run step with no output file, and nothing captured or checked, always runs.
- A script named only as a plain argument string (`&.{ "sh", "gen.sh" }`) is not an input of the Run step. Editing it
  does not rerun the step; `addFileInput(b.path("gen.sh"))` fixes that, and is left out of most snippets to keep them
  comparable.
- There are no custom steps. `Step.init` takes a `tag` from a fixed list, with no `id` or `makeFn`.
- A cache file deleted by hand is not recreated: the build fails with `FileNotFound` until the inputs change
  (`secondary`, `system2`).

### basic: copy `input` to `output`; rebuild if the input changes or the output is missing

**make** (GNU Make)
```make
output: input
	cp input output
```

**ninja**
```ninja
rule cp
  command = cp $in $out

build output: cp input
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const cp = b.addSystemCommand(&.{"cp"});
    cp.addFileArg(b.path("input"));
    const output = cp.addOutputFileArg("output");
    b.getInstallStep().dependOn(&b.addInstallFile(output, "output").step);
}
```

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep copy = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"output", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}
```

**Verified:** make: first run copied, second said up to date, after changing `input` it copied again, after deleting `output`
it copied again. ninja: same four results (`[1/1] cp input output`, then `no work to do`, then reruns). (Make 3.81 compares
whole seconds, so I slept 1s before each change.) Zig 0.17.0: the snippet ran unchanged. The first `zig build` copied `input` into the cache (`.zig-cache/o/<hash>/output`) and installed it as `zig-out/output`; a second run reported both steps `cached`; after appending to `input` the `cp` ran again; after deleting `zig-out/output` only the install step ran (the copy came from the cache and `cp` stayed `cached`).
**How each handles it:** make natively; ninja natively; Zig natively (the output lives in the cache and is installed to `zig-out/output`, not `./output`; deleting the installed file restores it from the cache without rerunning `cp`); smith natively, one step.
**What smith's API needs:** nothing new (`smith_step`, `smith_default`: v1).

### parallel: two independent outputs built at the same time

**make** (GNU Make)
```make
.PHONY: all
all: output1 output2

output1: input1
	sh run.sh input1 output1

output2: input2
	sh run.sh input2 output2
```
(`run.sh` logs `start`, sleeps 1s, copies, logs `end`.) Needs `make -j2`.

**ninja**
```ninja
rule run
  command = sh run.sh $in $out

build output1: run input1
build output2: run input2
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    inline for (.{ "1", "2" }) |n| {
        const run = b.addSystemCommand(&.{"sh"});
        run.addFileArg(b.path("run.sh"));
        run.addFileArg(b.path("input" ++ n));
        const output = run.addOutputFileArg("output" ++ n);
        b.getInstallStep().dependOn(&b.addInstallFile(output, "output" ++ n).step);
    }
}
```

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *outputs = (const char *[]){nullptr};
    for (int i = 1; i <= 2; i++) {
        SmithStep run = {
            .command = (const char *[]){"sh", "run.sh", "$in", "$out", nullptr},
            .implicit_inputs = (const char *[]){"run.sh", nullptr},
            .inputs = (const char *[]){smith_fmt(b, "input%d", i), nullptr},
            .outputs = (const char *[]){smith_fmt(b, "output%d", i), nullptr},
        };
        smith_step(b, &run);
        outputs = smith_cat(b, outputs, run.outputs, nullptr);
    }
    smith_default(b, outputs);
}
```
(In the ninja above `run.sh` is not listed as an input, matching the shootout's black-box rule; the smith version lists
it as an implicit input, which is more correct. Add `implicit_inputs = run.sh` to the make and ninja versions if
wanted.)

**Verified:** make: `make -j2` took 1.03s for two 1s jobs and the log read `start start end end`; a second run did nothing.
Without `-j` make would run them one after the other. ninja: plain `ninja` took 1.02s, log `start start end end`, second run
`no work to do`. ninja is parallel by default (cpus + 2 jobs). Zig 0.17.0: ran unchanged. After changing both inputs, `zig build` took 1.10s for two 1s jobs and the log read `start start end end`; a warm no-op took 0.08s. Outputs are `zig-out/output1` and `zig-out/output2` (the Run step's working folder is the project folder, so `run.sh` wrote `log` there).
**How each handles it:** make natively but only with `-j`; ninja natively and by default; Zig natively and by default (independent steps run in parallel by default; `-jN` limits it); smith natively, because ninja does it (and `b->cpus` is available).
**What smith's API needs:** nothing new (`smith_fmt`: v1). `smith build -j` passes through to ninja; with no `-j`,
ninja's default applies.

### include: compile a C file and rebuild when a transitively included header changes

**make** (GNU Make)
```make
main.o: include-main.c
	clang -MMD -MT main.o -MF main.d -c include-main.c -o main.o

-include main.d
```

**ninja**
```ninja
rule cc
  command = clang -MMD -MT $out -MF $out.d -c $in -o $out
  depfile = $out.d
  deps = gcc

build main.o: cc include-main.c
```
(smith writes `rule step`, `cmd = clang -MMD ...` and `depfile = main.o.d` on the edge; the rule above is the plain form.)

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const cc = b.addSystemCommand(&.{ "clang", "-MMD", "-c" });
    cc.addFileArg(b.path("include-main.c"));
    cc.addArg("-MF");
    _ = cc.addDepFileOutputArg("main.d");
    cc.addArg("-o");
    const object = cc.addOutputFileArg("main.o");
    b.getInstallStep().dependOn(&b.addInstallFile(object, "main.o").step);
}
```

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep compile = {
        .command =
            (const char *[]){"clang", "-MMD", "-MT", "main.o", "-MF", "main.o.d", "-c", "$in", "-o", "$out", nullptr},
        .inputs = (const char *[]){"include-main.c", nullptr},
        .outputs = (const char *[]){"main.o", nullptr},
        .depfile = "main.o.d",
    };
    smith_step(b, &compile);
    smith_default(b, compile.outputs);
}
```

**Verified:** used `clang` (Apple's) with `include-main.c` -> `include-1.h` -> `include-2.h`. make: first run compiled, second
said up to date, after appending to `include-2.h` it recompiled, then up to date. ninja: identical, with `deps = gcc`; I also
ran it without `deps = gcc` (depfile only) and it behaved the same, but then the `.d` file stays on disk and is reparsed each
time. With `deps = gcc` ninja ingests the depfile into `.ninja_deps` and deletes it. Zig 0.17.0: ran unchanged with Apple's `clang` (output `zig-out/main.o`). Second run `cached`; `touch include-2.h` (same contents) stayed `cached`; appending a line to `include-2.h` recompiled; the next run was `cached`. Files named in a depfile are compared by content, so a touch does not rebuild (unlike make and ninja).
**How each handles it:** make with a workaround (`-include main.d`, and the first run has no `.d`); ninja natively (`depfile`); Zig natively (`addDepFileOutputArg`; the files it names are compared by content, not timestamp); smith natively through `.depfile`.
**What smith's API needs:** nothing new in the struct (`.depfile`: v1). Decision for smith: whenever `.depfile` is set,
emit `deps = gcc` as well (the field is documented as `-MMD` form); that keeps `.d` files out of the tree and is what
the shootout does. A tool that writes MSVC `/showIncludes` output would need a separate field (`deps = msvc`), but
nothing here needs it.

### wildcard: build `X.out` from `X.in` for any `X`, chosen on the command line

**make** (GNU Make)
```make
%.out: %.in
	cp $< $@
```
Run as `make name1.out`.

**ninja**
```ninja
rule cp
  command = cp $in $out

build name1.out: cp name1.in
```
ninja has no patterns. The edge has to be written for each file, which means a generator has to write it.

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const io = b.graph.io;
    // The configure result is cached; tell Zig which directory it was computed from.
    b.dependOnDirectoryContents(b.path("."));
    var dir = b.root.root_dir.handle.openDir(io, ".", .{ .iterate = true }) catch @panic("open");
    defer dir.close(io);
    var it = dir.iterate();
    while (it.next(io) catch @panic("iterate")) |entry| {
        if (!std.mem.endsWith(u8, entry.name, ".in")) continue;
        const stem = entry.name[0 .. entry.name.len - ".in".len];
        const cp = b.addSystemCommand(&.{"cp"});
        cp.addFileArg(b.path(b.dupe(entry.name)));
        const name = b.fmt("{s}.out", .{stem});
        const output = cp.addOutputFileArg(name);
        const install = b.addInstallFile(output, name);
        b.getInstallStep().dependOn(&install.step);
        b.step(name, "build just this file").dependOn(&install.step); // zig build name1.out
    }
}
```
Changes from the 0.15 version: `b.build_root` no longer exists (the project folder is `b.root.root_dir.handle`), file-system calls take the `io` from `b.graph.io`, and the last two lines add a step per file so `zig build name1.out` builds just that file. `b.dependOnDirectoryContents` is required: see Verified.

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *outputs = (const char *[]){nullptr};
    for (const char *const *in = smith_glob(b, (const char *[]){"*.in", nullptr}); *in; in++) {
        SmithStep copy = {
            .command = (const char *[]){"cp", "$in", "$out", nullptr},
            .inputs = (const char *[]){*in, nullptr},
            .outputs = (const char *[]){smith_replace_extension(b, *in, ".out"), nullptr},
        };
        smith_step(b, &copy);
        outputs = smith_cat(b, outputs, copy.outputs, nullptr);
    }
    smith_default(b, outputs);
}
```
(`smith build name1.out` builds just that file, and `smith build` builds them all through the default.)

**Verified:** make: `make name1.out` copied, a second run was up to date, after changing `name1.in` it copied again, and the
file held the new content. ninja: with the generated edge for `name1.in`, the same four results. A new file `name2.in` would
need `build.ninja` regenerated: that is what rerunning build.c on every `smith build` does, and `smith_glob` then returns
the new file; I did not simulate the rerun here, only the generated edge. Zig 0.17.0: the original did not compile (`no field named 'build_root' in struct 'Build'`). The version above: `zig build name1.out` copied to `zig-out/name1.out`; again `cached`; after changing `name1.in` the copy reran and held the new content; adding `name2.in` made the next `zig build` build `zig-out/name2.out` (the configure phase reran because the directory listing changed), and `zig build name3.out` right after adding `name3.in` worked; removing `name2.in` left `zig-out/name2.out` behind. Without the `dependOnDirectoryContents` line the same sequence did NOT notice the new `name2.in`: 0.17 caches the result of running `build.zig`, so it is not rerun on every invocation as the 0.15 note claimed.
**How each handles it:** make natively (pattern rule); ninja not at all on its own, only through a generator; Zig with a workaround that is just Zig code (directory iteration), plus a declaration: `build.zig` is not rerun on every `zig build` in 0.17, so a new file is missed unless the build declares the directory with `b.dependOnDirectoryContents`; smith through `smith_glob` plus a C loop, with `build.c` rerun on every `smith build` so a new file is seen.
**What smith's API needs:** `smith_glob` (v1) and `smith_replace_extension` (v1). `smith_glob` is read on every
`smith build`, so a new or removed file is seen, and it returns a sorted list, so `build.ninja` is stable and a rerun
does not look like a change. `smith_replace_extension` is a convenience, not required for correctness: without it, `X.out` from `X.in` needs
`strlen` and `%.*s` through `smith_fmt`. With it, every compile-to-object rule reads better.

### spaces: input and output file names containing spaces

**make** (GNU Make)
```make
output\ file: input\ file
	cp "input file" "output file"
```
Run as `make "output file"`.

**ninja**
```ninja
rule cp
  command = cp $in $out

build output$ file: cp input$ file
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const cp = b.addSystemCommand(&.{"cp"});
    cp.addFileArg(b.path("input file"));
    const output = cp.addOutputFileArg("output file");
    b.getInstallStep().dependOn(&b.addInstallFile(output, "output file").step);
}
```

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep copy = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"input file", nullptr},
        .outputs = (const char *[]){"output file", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}
```
smith must write the path as `output$ file` in the `build` line (ninja escapes space, colon and `$` with `$`) and rely
on ninja's own quoting of `$in` / `$out`. Literal words in `command` that contain spaces or shell characters must be
single-quoted by smith.

**Verified:** make: with the recipe quoted by hand it copied, was up to date, and recopied after the input changed. The
rule line needs backslashes and the recipe needs quotes (two different escapings). ninja: ran `[1/1] cp 'input file' 'output file'`,
so ninja quotes `$in` and `$out` itself; no-op on the second run; reran after the change. This is why the whole-word `$in`
/ `$out` design works. Make does not quote `$<` / `$@` for you, so `cp $< $@` would break here. Zig 0.17.0: ran unchanged. Output `zig-out/output file`; second run `cached`; after appending to `input file` it reran and the copy held both lines.
**How each handles it:** make with a workaround (backslash in the rule, quotes in the recipe, and `$(wildcard)` / `$(foreach)` break on spaces); ninja natively (`$ ` escape plus automatic quoting of `$in` / `$out`); Zig natively (argv is an array, no quoting); smith natively, since `.command` is an argv and smith does the escaping when writing build.ninja.
**What smith's API needs:** nothing new (v1). Requirement on the implementation: escape paths in `build` lines (`$ `,
`$:`, `$$`) and shell-quote literal command words; add a test with a space in a name.

### monad1: depend on the files listed in another file (`cat list | xargs cat > output`)

**make** (GNU Make)
```make
output: list $(shell cat list)
	cat $(filter-out $<,$^) > $@
```

**ninja**
```ninja
rule step
  command = $cmd

build output: step list input1 input2
  cmd = sh -c 'cat "$$@" > "$$0"' output input1 input2
```
This file is only right while `list` says `input1 input2`. ninja cannot read `list`; something has to regenerate the file
when `list` changes. In the real shootout ninja version the command itself writes a depfile (`cat $in | xargs echo $out:`),
so after the first run ninja learns the dependencies from the output of the step.

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const io = b.graph.io;
    b.dependOnFileContents(b.path("list")); // the configure result is cached; rerun it when list changes
    const list = b.root.root_dir.handle.readFileAlloc(io, "list", b.allocator, .limited(1 << 20)) catch @panic("list");
    const cat = b.addSystemCommand(&.{"cat"});
    var lines = std.mem.tokenizeScalar(u8, list, '\n');
    while (lines.next()) |name| cat.addFileArg(b.path(name));
    const output = cat.captureStdOut(.{});
    b.getInstallStep().dependOn(&b.addInstallFile(output, "output").step);
}
```
Changes from the 0.15 version: `b.build_root` is gone, `readFileAlloc` takes `io` and a limit, `captureStdOut` takes an options struct, and the `dependOnFileContents` line is needed: `zig build` caches the result of running `build.zig` in 0.17, so a changed `list` is only seen if the build declares it.

**smith build.c** (v1.5)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *names = smith_lines(b, "list"); // read on every smith build
    SmithStep concatenate = {
        .command = smith_cat(b, (const char *[]){"sh", "-c", "cat \"$@\" > \"$0\"", "output", nullptr}, names, nullptr),
        .inputs = smith_cat(b, (const char *[]){"list", nullptr}, names, nullptr),
        .outputs = (const char *[]){"output", nullptr},
    };
    smith_step(b, &concatenate);
    smith_default(b, concatenate.outputs);
}
```
The file names are literal words of the command, so smith shell-quotes them. A step written as a C function (`.run`,
later) could do the same without the shell.

**Verified:** make (3.81): first run built, second up to date, after `input1` changed it rebuilt and had `more again`, after
`list` was cut to `input1` it rebuilt and had `more`, then up to date, then changing `input2` (no longer listed) rebuilt
nothing. (Run 1 of this had a false result: `list` was written in the same second as `output`, and make 3.81 compares whole
seconds. With a 1s sleep it is correct.) ninja: with the static file it built, was a no-op, rebuilt after `input1` changed
and had `more again`. Then, to simulate `smith` rerunning `build.c` after `list` changed, I wrote the second `build.ninja`
(`build output: step list input1`, command listing only `input1`): ninja reran because the command text differed, output was
`more`, a second run was a no-op, and changing `input2` ran nothing. So the generated-file approach works on ninja, as long as
smith reruns `build.c` when `list` changes. `build.c` itself cannot be run, so the rerun was done by hand. Zig 0.17.0: the original did not compile (`b.build_root`, and the old `readFileAlloc` and `captureStdOut()` signatures). With `list` = `input1`, `input2`: output `more x`; again `cached`; changing `input1` reran; cutting `list` to `input1` reran and gave `more again`; changing the unlisted `input2` stayed `cached`; touching `list` alone stayed `cached`. Output is `zig-out/output`. Without the `dependOnFileContents` line, editing `list` was not noticed and the old output stayed.
**Run under smith v1.5** (the snippet above, unchanged, in a scratch folder with `input1`, `input2` and `list`; ninja 1.13.2): the first `smith build` ran `sh -c 'cat "$@" > "$0"' output input1 input2` and `output` held both files; a second run said `no work to do`; after `input1` changed it reran; after `list` was cut to `input1` it reran with only `input1` in the command; then `no work to do`; changing the unlisted `input2` ran nothing. The snippet needed no fix.
**How each handles it:** make natively (`$(shell cat list)` is read each run); ninja not natively (static graph; works only with a depfile written by the command, or a generator that rereads `list`); Zig with a declaration: the graph is built by Zig code that reads `list`, but 0.17 caches the configuration, so `b.dependOnFileContents(b.path("list"))` is what makes a changed `list` rebuild the graph (without it the change was missed); smith with the new read, because `build.c` reruns on every `smith build`, so a changed `list` is seen.
**What smith's API needs:** `smith_lines` (v1.5), a file read that a later cache could track. In v1 plain `fopen` in
`build.c` would also work, because the program reruns every time, but the call keeps the read visible to the checks:

```c
// the file's lines, read on every `smith build`
const char *const *smith_lines(SmithBuild *b, const char *path);
```
The result has one entry per line without its newline (a final newline adds no entry, an empty line is `""`), and an error names the file if it is missing. (A whole-text
`smith_read` was proposed with `smith_lines` as its split. One of the two is enough, and `smith_lines` is the one this
case uses, so `smith_read` was dropped.)

### monad2: the list of inputs is itself generated, and the final step reads the files it names

Scenario: `source` holds file names; `list` is a copy of it; `output` is the concatenation of every file named in `list`. Changing `source` to name different files must change `output`; touching a file that is named must rebuild `output`; touching a file that is not named must not.

**make** (GNU Make)
```make
output: list
	cat list | xargs cat > output
	cat list | xargs echo output: > output.d

list: source
	cp source list

-include output.d
```

**ninja**
```ninja
rule copy
    command = cp $in $out

rule cat_list
    command = cat $in | xargs cat > $out && cat $in | xargs echo $out: > $depfile
    depfile = $out.d

build list: copy source
build output: cat_list list
```
Alternative with `dyndep` (also run, also works):
```ninja
ninja_required_version = 1.10

rule copy
    command = cp $in $out

rule mkdd
    command = sh mkdd.sh $in $target $out

rule cat_list
    command = cat list | xargs cat > $out

build list: copy source
build output.dd: mkdd list
    target = output
build output: cat_list list || output.dd
    dyndep = output.dd
```
with `mkdd.sh list output output.dd` writing `ninja_dyndep_version = 1` and `build output: dyndep | a.txt b.txt` (the names in `list`).

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const mklist = b.addSystemCommand(&.{"cp"});
    mklist.addFileArg(b.path("source"));
    const list = mklist.addOutputFileArg("list");

    // a depfile written by the command: Zig reads it after the run and tracks the files it names
    const cat = b.addSystemCommand(&.{ "sh", "cat-list.sh" }); // cat-list.sh list output output.d
    cat.addFileArg(list);
    const output = cat.addOutputFileArg("output");
    _ = cat.addDepFileOutputArg("output.d");

    b.getInstallStep().dependOn(&b.addInstallFile(output, "output").step);
}
```
`cat-list.sh list output output.d` is three lines: `cat "$1" | xargs cat > "$2"` and `cat "$1" | xargs echo "$2:" > "$3"` (plus an `echo run >> run.log` to count runs). The script is named as a plain argument string, which Zig does not track as an input: editing `cat-list.sh` left the step `cached`; `cat.addFileInput(b.path("cat-list.sh"))` makes edits to it rerun the step (verified, left out of the snippet to keep it comparable).

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep list = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"source", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    SmithStep output = {
        .command =
            (const char *[]){"sh", "-c", "cat list | xargs cat > output && cat list | xargs echo output: > output.d",
                             nullptr},
        .inputs = list.outputs,
        .outputs = (const char *[]){"output", nullptr},
        .depfile = "output.d",
    };
    smith_step(b, &output);
    smith_default(b, output.outputs);
}
```

**Verified:** make: ran `make` (builds list, output, output.d), `make` again (up to date), touched `a.txt` (named: output rebuilt), changed `source` to name only `a.txt` (list and output rebuilt, output holds only a's text), touched `b.txt` (no longer named: nothing ran). Passes. An earlier trial without sleeps showed output not rebuilt, which was whole-second mtime granularity in make 3.81, not a build bug. ninja, depfile form: same five steps, same results (`ninja: no work to do.` after a second run and after touching the unnamed file). ninja, dyndep form: same five steps, same results; the dyndep file is rebuilt whenever `list` changes, and the edge for `output` gains the named files as implicit inputs. The depfile form is simpler because it needs no extra edge. Zig 0.17.0: the snippet ran unchanged (output `zig-out/output`). First run built `list` then `output`; again `cached`; touching `a.txt` stayed `cached` (depfile files are compared by content); changing `a.txt` reran only the `cat` step (`cp` stayed `cached`) and `output` held the new text; changing `source` to name only `a.txt` reran both and `output` held only a's text; changing the no-longer-named `b.txt` stayed `cached`.
**How each handles it:** make: natively (a generated `output.d` included with `-include`, make restarts after remaking it). ninja: natively, with `depfile` (or `dyndep`) for files that already exist. Zig: natively, `addDepFileOutputArg` (the named files are compared by content, so a touch does not rerun). smith: natively, the `depfile` field; nothing new.
**What smith's API needs:** nothing new (`.depfile`: v1). Files named by the list must already exist or be built by a
step listed somewhere else; for that case see monad3.

### monad3: the generated list names files that must themselves be generated, by steps that only exist if the list says so

Scenario (the game shape: a program prints the mesh list, the rest of the build depends on it): `source` names meshes; `list` is a copy of it; for every name `n` in `list` there is a new step `m/n.src -> out/n.o` (`tr a-z A-Z`); `output` concatenates all the `out/*.o`. Changing `source` must add and remove steps; touching `m/cube.src` must rebuild only `out/cube.o` and `output`.

**make** (GNU Make)
```make
all: output

list: source
	cp source list

list.mk: list
	awk '{ print "OBJS += out/" $$1 ".o"; print "out/" $$1 ".o: m/" $$1 ".src"; print "\tmkdir -p out; tr a-z A-Z < $$< > $$@" }' list > list.mk

-include list.mk

output: $(OBJS)
	cat $(OBJS) > output
```

**ninja**
Stage 1 manifest (what smith writes first; `gen.sh` stands in for the compiled build program, which is re-run to write stage 2):
```ninja
ninja_required_version = 1.10
rule copy
    command = cp $in $out
rule regen
    command = sh gen.sh
    generator = 1
build list: copy source
build build.ninja: regen list | gen.sh
```
Stage 2 manifest (written by the build program once `list` exists; the stage 1 lines are repeated unchanged, so the loop closes):
```ninja
ninja_required_version = 1.10
rule copy
    command = cp $in $out
rule conv
    command = tr a-z A-Z < $in > $out
rule cat_all
    command = cat $in > $out
rule regen
    command = sh gen.sh
    generator = 1
build list: copy source
build build.ninja: regen list | gen.sh
build out/cube.o: conv m/cube.src
build out/sphere.o: conv m/sphere.src
build output: cat_all out/cube.o out/sphere.o
default output
```
The `dyndep` attempt (fails, shown for the record): a dyndep file may only name edges that already exist in the manifest.
```ninja
# dyndep file: build output: dyndep | out/cube.o
#              build out/cube.o: dyndep          <- ninja: no build statement exists for 'out/cube.o'
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

// Stage 1 is the `list` step: it copies source to list in the source tree (list is generated, not committed).
// Stage 2 is this function on the next `zig build`: configuration reads list and creates one Run step per name.
pub fn build(b: *std.Build) void {
    const io = b.graph.io;

    const mklist = b.addSystemCommand(&.{"cp"});
    mklist.addFileArg(b.path("source"));
    const list = mklist.addOutputFileArg("list");
    const update = b.addUpdateSourceFiles();
    update.addCopyFileToSource(list, "list");
    b.step("list", "write list from source").dependOn(&update.step);

    // list does not exist before stage 1 has run; dependOnFileContents fails on a missing file, so watch the folder for it
    b.dependOnDirectoryContents(b.path("."));
    const text = b.root.root_dir.handle.readFileAlloc(io, "list", b.allocator, .limited(1 << 20)) catch return;
    b.dependOnFileContents(b.path("list"));

    const cat = b.addSystemCommand(&.{"cat"});
    var names = std.mem.tokenizeScalar(u8, text, '\n');
    while (names.next()) |n| {
        const conv = b.addSystemCommand(&.{ "tr", "a-z", "A-Z" });
        conv.setStdIn(.{ .lazy_path = b.path(b.fmt("m/{s}.src", .{n})) });
        cat.addFileArg(conv.captureStdOut(.{ .basename = b.fmt("{s}.o", .{n}) }));
    }
    b.getInstallStep().dependOn(&b.addInstallFile(cat.captureStdOut(.{}), "output").step);
}
```
`zig build list` is stage 1 and plain `zig build` is stage 2: `list` is written into the source tree (`UpdateSourceFiles`), and the next configure reads it and creates one `tr` step per name. `source` here names `cube` and `sphere`; `m/*.src` are the inputs.

**smith build.c** (v1.5)
```c
#include <smith/build.h>

// stage 2: called when "list" exists
static void meshes(SmithBuild *b) {
    const char *const *names = smith_lines(b, "list"); // one entry per line
    for (const char *const *n = names; *n; n++) {
        SmithStep convert = {
            .command = (const char *[]){"sh", "-c", "tr a-z A-Z < \"$1\" > \"$2\"", "sh", "$in", "$out", nullptr},
            .inputs = (const char *[]){smith_fmt(b, "m/%s.src", *n), nullptr},
            .outputs = (const char *[]){smith_fmt(b, "out/%s.o", *n), nullptr},
        };
        smith_step(b, &convert);
    }
    SmithStep all = {
        .command = (const char *[]){"sh", "-c", "cat \"$@\" > output", "sh", "$in", nullptr},
        .inputs = smith_map(b, names, "out/%s.o"),
        .outputs = (const char *[]){"output", nullptr},
    };
    smith_step(b, &all);
    smith_default(b, all.outputs);
}

void build(SmithBuild *b) {
    SmithStep list = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"source", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    smith_defer(b, list.outputs, meshes);
}
```

**Verified:** make: `make` builds list, restarts after remaking `list.mk`, builds both objects and output; second `make` does nothing; changing `source` from `cube sphere` to `cube tri` rebuilt list, list.mk, `out/tri.o`, output (output = CUBE1, TRI1); touching `m/cube.src` rebuilt only `out/cube.o` and output; again does nothing. Passes ("requires automatic restarting" in the README is exactly this `-include`).

ninja, `depfile`: does not work for monad3. A depfile can only report inputs after the command has run, and ninja never builds them: with `gen` named in `list`, `cat: gen: No such file or directory` and the build fails (it only worked when `gen` happened to be a default root target). ninja, `dyndep`: works when the candidate edges already exist in the manifest (the shootout's own case: `gen` is generated only if listed; I wrote `build gen: gen` statically and the dyndep file adds `| gen` to `output`; with `default output`, `gen` was built only when listed, and listing it again later rebuilt output; verified). It fails for the game shape: a dyndep file naming an edge that is not in the manifest is rejected (`no build statement exists for 'out/cube.o'`), and naming only an input leaves it with no rule (`cat: out/*.o: No such file or directory`). So dyndep cannot create new steps, as the brief says.

ninja, second stage by manifest regeneration (hand-written stage 1 and stage 2 above, `gen.sh` standing in for the build program): verified with ninja 1.13.2.
- Fresh tree, stage 1 manifest only: one `ninja` ran `cp source list`, then `sh gen.sh` (rewrote build.ninja), then ninja reloaded the manifest, restarted its step counter (`[1/5]`...) and built both objects and `output` in the same invocation. Output correct.
- Second `ninja`: `no work to do`. The generator did not rerun.
- Changed `source` (`cube sphere` to `cube tri`): list rebuilt, generator reran, new edge for tri built, output rebuilt, in one invocation. 
- Touched `m/cube.src`: only `out/cube.o` and `output` ran; the generator did not.
- Touched `gen.sh` (the build program changed): generator reran, manifest reloaded, nothing else ran. This is item 4 of the smith build pipeline for free.
- `ninja output` on a fresh tree, where `output` is not in the stage 1 manifest: works, because ninja rebuilds the manifest before looking up the named targets.
- A stage 2 that fails (`list` names a mesh with no `m/x.src`): ninja stops with `'m/missing.src', needed by 'out/missing.o', missing and no known rule`; fixing `source` and running `ninja` again recovers.
- Limits found: (1) `ninja -n output` on a fresh tree only reports stage 1 (`cp`, `gen`) and stops, since a dry run cannot reload; ninja's `-t` tools and `-n` see whichever manifest is on disk. (2) Manifest rebuild ignores the build log: it is decided by mtimes only. Consequence: if smith rewrites a stage 1 manifest over an up-to-date tree, the new manifest is newer than `list`, so ninja sees nothing to do and `output` never appears (verified, even with a different command string). The fix, verified: smith writes the stage 1 manifest and then sets its mtime to the past (`utimes`, 1970), or else calls `more` whenever the needs already exist. (3) Files in `out/` for names that left the list stay behind (this is the `noleftover` case). (4) The generator edge needs `generator = 1` so `ninja -t clean` keeps `build.ninja` (verified: `Cleaning... 3 files`, manifest kept).
- Not run: putting the compile of `build.c` into the same manifest as an edge feeding the regen edge (extrapolated from the `gen.sh` touch test).
- Zig 0.17.0: the 0.15 custom step does not exist any more. `Step.init` takes a `tag` from a fixed list (`Step.Tag`: compile, run, install_file, ...) and has no `id`, `.custom` or `makeFn`, and the first compile error was `no field named 'id' in struct 'Build.Step.StepOptions'`; the build runner (`compiler/Maker.zig`) configures once per invocation, before any step runs, so no step can be added from a build product. What does work is two invocations (clean tree, then `list`): plain `zig build` before `list` exists builds nothing; `zig build list` wrote `list`; `zig build` then ran two `tr` steps and `cat` (output `zig-out/output` = `CUBE1 SPHERE1`); again all `cached`; `touch m/cube.src` reran only the cube `tr` and `cat`; changing `m/cube.src` the same; changing `source` to `cube tri` then `zig build list; zig build` reran the new `tri` `tr` and `cat` (the cube `tr` stayed `cached`) and `output` became `CUBE2 TRI1`. `b.dependOnFileContents(b.path("list"))` on a missing `list` fails the configure with `FileSystemFailure` although its doc comment says creation is tracked, hence the directory dependency. `zig build list install --watch` converged by itself in one process: pass one wrote `list`, the watcher saw the dependency change, reconfigured, and pass two built `output`. Without `list` run first, a changed `source` is not picked up by a plain `zig build` (stale output, verified).
**Run under smith v1.5** (the snippet above, unchanged, with `source` = `cube sphere` and `m/{cube,sphere,tri}.src`; ninja 1.13.2):
- Fresh tree: one `smith build` ran `cp source list`, the regeneration, then both `tr` steps and the `cat`; `output` = `CUBE1 SPHERE1`. The second run: `no work to do`.
- `source` changed to `cube tri`: one `smith build` ran `cp`, the regeneration, `out/tri.o` and `output` only (`CUBE1 TRI1`).
- `m/cube.src` changed: only `out/cube.o` and `output` ran, no regeneration; then `no work to do`.
- Fresh tree, `smith build output` (a target that exists only in stage 2): worked in one call.
- `smith build -t clean` (`Cleaning... 4 files`) then `smith build`: rebuilt everything; the manifest survived the clean.
- A `source` naming `missing` stopped with `'m/missing.src', needed by 'out/missing.o', missing and no known rule`; fixing `source` and running `smith build` again recovered.
The snippet needed no fix. The backdating rule below is not needed: when ninja runs the program through the regeneration edge (`--regenerate`), the program always rewrites the manifest, so its mtime is past the needs; the run before ninja writes a manifest only when its bytes changed.
**How each handles it:** make: natively, with automatic restart of included makefiles (`list.mk`). ninja: `dyndep` only for edges that exist in advance; new edges only by regenerating the manifest (works, tested above); a depfile does not work. Zig: with a workaround. Not within one `zig build`: configuration runs once before any step, so a step cannot be added from a build product, and the 0.15 custom step is gone. With two invocations (`zig build list`, then `zig build`) it adds and removes per-name steps and caches them per name; `--watch` does both in one process. smith: with a new `smith_defer`, compiled to the manifest regeneration above.
**What smith's API needs:**
- `smith_defer` (v1.5):
  ```c
  void smith_defer(SmithBuild *b, const char *const *needs, void (*more)(SmithBuild *b));
  ```
`needs` are outputs of steps already declared. smith writes them and a regen edge (`build build.ninja: <build program>
<needs> | <build program binary>`, `generator = 1`) and calls `more` only when every need already exists. `more` may
call `smith_step`, `smith_alias` and `smith_default`. The regen edge is kept in every later manifest, so the loop is
closed.
- `smith_lines` (v1.5): a read of a file produced by an earlier step, one entry per line. The file is a need, so it
  exists when `more` runs; without `smith_lines`, `more` would use stdio.
- `smith_map` (v1): a new list with `format` applied to each item (`"out/%s.o"`), to build the input list of the final
  step without a macro.
- In v1's layout the manifest is `.smith/build.ninja` and the regen edge runs `.smith/program`; the blocks
  above keep the names of the hand-run test.
- Rule for the writer, as built: the program calls `more` whenever the needs exist, and the manifest is rewritten
  unconditionally when ninja runs the program with `--regenerate`; otherwise it is written only when its bytes changed.
  No backdating.

**Recommendation (monad2 and monad3):** use the second stage (`smith_defer`, implemented as a self-regenerating
manifest) for anything that adds steps, and keep `depfile` for monad2 (files that already exist). Do not build on
`dyndep`. Reasons:
1. `dyndep` cannot add steps, so it fails exactly the game case (one conversion step per mesh in a generated list). The
   part it can do (a known set of steps becomes inputs of an edge) is rare and needs a generated dyndep file plus an
   edge for it.
2. The regenerated manifest works with a plain `ninja` and with `ninja <target>`, rebuilds incrementally (only what
   changed in the list), and survives a second run and a changed build program, all verified.
3. It reuses the same mechanism smith needs anyway to rerun `build.c` when it changes, so there is one loop rather than
   two.
4. The costs are known and small: `ninja -n` is stage 1 only, and smith must backdate the stage 1 manifest. A driver
   alternative (smith itself runs `ninja <needs>`, then the build program again, then `ninja`) would avoid the
   backdating but would make plain `ninja` stop working, so it is the worse choice.

### unchanged: a generator reruns but produces the same bytes, so what depends on it must not rerun

Scenario: `source` is generated from `input` by `gen.sh input source`, which rewrites `source` only if its content would change; `output` is made from `source` by `run.sh source output`. Touching `input` without changing what `source` would be must not rerun `run.sh`.

**make** (GNU Make)
```make
output: source
	sh run.sh source output

source: input
	sh gen.sh input source
```

**ninja**
```ninja
rule gen
    command = sh gen.sh $in $out
    restat = 1
rule run
    command = sh run.sh $in $out
build source: gen input
build output: run source
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const gen = b.addSystemCommand(&.{ "sh", "gen.sh" });   // gen.sh input source (always writes a fresh file here)
    gen.addFileArg(b.path("input"));
    const source = gen.addOutputFileArg("source");

    const run = b.addSystemCommand(&.{ "sh", "run.sh" });   // run.sh source output
    run.addFileArg(source);
    const output = run.addOutputFileArg("output");

    b.getInstallStep().dependOn(&b.addInstallFile(output, "output").step);
}
```
`gen.sh` and `run.sh` are the same scripts as in the make and ninja runs. A change from the case's own scenario matters here: see Verified.

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep source = {
        .command = (const char *[]){"sh", "gen.sh", "$in", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"source", nullptr},
        .restat = true,
    };
    smith_step(b, &source);
    SmithStep output = {
        .command = (const char *[]){"sh", "run.sh", "$in", "$out", nullptr},
        .inputs = source.outputs,
        .outputs = (const char *[]){"output", nullptr},
    };
    smith_step(b, &output);
    smith_default(b, output.outputs);
}
```

**Verified:** make: first build runs both; second does nothing; `touch input` (same content) ran only `gen.sh`, not `run.sh`; changing the content ran both. So the `output` step is protected, contrary to the README's "failure". But make never gets clean: with `source` still older than `input`, every later `make` reruns `gen.sh` until the content changes (verified, `sh gen.sh input source` printed on every run). ninja with `restat = 1`: touch with same content ran only the generator (`[1/2]`, the second step pruned), the next `ninja` printed `no work to do` (ninja records the restat result), and a content change ran both. Without `restat` the touch ran both steps, as the README says. Zig 0.17.0: the snippet ran unchanged (output `zig-out/output`). First build ran both; again `cached`; `touch input` (same bytes) and a same-bytes rewrite of `input` each reran only `gen.sh` while `run` stayed `cached` (`run.log` stayed at 1 line); changing `input` ran both. But the README's case is stronger: `input` changes and `source` comes out byte-identical (input `in` to `out`, both give `source` = `out`): `run.sh` reran (`run.log` grew by a line). A step's outputs live in a cache folder named by a hash of its inputs, and the next step's key includes that path, not the bytes. So Zig does not cut off on unchanged output; the touch result above is a cache hit on identical inputs. With `captureStdOut` instead of an output file even the touch reran the next step.
**How each handles it:** make: natively but the generator reruns on every build until `input` changes content. ninja: natively with `restat = 1`. Zig: partly. A touch or same-bytes rewrite of `input` reruns the generator but not `run` (identical inputs give the same cache folder); but when `input` changes and the generated file comes out identical, `run` reruns, because Zig compares the generator's inputs, not its output; that is the README's case, so Zig does not do it. smith: natively with `.restat = true`.
**What smith's API needs:** nothing new (`.restat`: v1).

### multiple: one step writes two files, only one of which changes

Scenario: `gen.sh input source1 source2` writes each source only if its own content would change; `output1` is made from `source1` and `output2` from `source2` (`run.sh in out`). Changing line 1 of `input` must rerun only `output1`.

**make** (GNU Make)
```make
output1: source1
	sh run.sh source1 output1

output2: source2
	sh run.sh source2 output2

# two targets of an explicit rule would run the recipe once per target; a pattern rule with two
# targets (stem "e") runs it once for both
sourc%1 sourc%2: input
	sh gen.sh input source1 source2
```

**ninja**
```ninja
rule gen
    command = sh gen.sh $in $out
    restat = 1
rule run
    command = sh run.sh $in $out
build source1 source2: gen input
build output1: run source1
build output2: run source2
```

**build.zig** (Zig 0.17.0, run, does not handle the case)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const gen = b.addSystemCommand(&.{ "sh", "gen.sh" });   // gen.sh input source1 source2
    gen.addFileArg(b.path("input"));
    const source1 = gen.addOutputFileArg("source1");
    const source2 = gen.addOutputFileArg("source2");

    const run1 = b.addSystemCommand(&.{ "sh", "run.sh" });
    run1.addFileArg(source1);
    const output1 = run1.addOutputFileArg("output1");
    const run2 = b.addSystemCommand(&.{ "sh", "run.sh" });
    run2.addFileArg(source2);
    const output2 = run2.addOutputFileArg("output2");

    b.getInstallStep().dependOn(&b.addInstallFile(output1, "output1").step);
    b.getInstallStep().dependOn(&b.addInstallFile(output2, "output2").step);
}
```
The snippet is the closest working one: both outputs are produced and installed, but the per-output cutoff is missing, as Verified says.

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep source = {
        .command = (const char *[]){"sh", "gen.sh", "$in", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"source1", "source2", nullptr},
        .restat = true,
    };
    smith_step(b, &source);
    SmithStep output1 = {
        .command = (const char *[]){"sh", "run.sh", "$in", "$out", nullptr},
        .inputs = (const char *[]){source.outputs[0], nullptr},
        .outputs = (const char *[]){"output1", nullptr},
    };
    smith_step(b, &output1);
    SmithStep output2 = {
        .command = (const char *[]){"sh", "run.sh", "$in", "$out", nullptr},
        .inputs = (const char *[]){source.outputs[1], nullptr},
        .outputs = (const char *[]){"output2", nullptr},
    };
    smith_step(b, &output2);
    smith_default(b, smith_cat(b, output1.outputs, output2.outputs, nullptr));
}
```

**Verified:** make, plain two-target rule `source1 source2: input`: after changing line 1 only, the right outputs were rebuilt (`output1` only) but the generator ran twice, once per target (`gen.log` shows two runs; harmless here because the generator is idempotent). Make with the pattern-rule trick: the generator ran once and only `output1` reran. ninja with `restat = 1`: first build ran all 3 steps, second did nothing, changing line 1 ran the generator and `output1` only (`[2/2]`), the next run did nothing; `run.log` shows `output1, output2, output1`. restat looks at each output of the edge separately, so this works.  Zig 0.17.0: the snippet ran unchanged (outputs `zig-out/output1` and `zig-out/output2`). First build ran gen, `run1` and `run2`; again `cached`; `touch input` reran only `gen.sh`; changing line 1 of `input` reran `run.sh` for BOTH outputs (`run.log` shows `output2` and `output1`), where make with the pattern rule and ninja with `restat` rerun only `output1`. Both sources of one step sit in one cache folder named by a hash of the step's inputs, so a change to the inputs changes both paths, and each consumer's key includes its input's path. Splitting the generator into one step per output would avoid it but changes the case (one generator writes both files).
**How each handles it:** make: works, with a pattern-rule trick to run the generator once (the plain form runs it once per target). ninja: natively, with `restat`. Zig: not at all (both outputs of one step share a cache folder named by the hash of its inputs, so any input change reruns both consumers; measured). smith: natively with `.restat = true`.
**What smith's API needs:** nothing new (`.restat`: v1).

### system1: a step that must run on every build, but what depends on it reruns only if its result changed

Scenario: `gen.sh source` reads the current system information (here the file `system1-data`, standing in for `gcc --version`) and rewrites `source` only if it differs; it must run on every build. `run.sh source output` must run only when `source` changed.

**make** (GNU Make)
```make
output: source
	sh run.sh source output

source: FORCE
	sh gen.sh source

FORCE:
```

**ninja**
```ninja
rule gen
    command = sh gen.sh $out
    restat = 1
rule run
    command = sh run.sh $in $out
build smith_always: phony
build source: gen | smith_always
build output: run source
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const gen = b.addSystemCommand(&.{ "sh", "gen.sh" });   // writes the system information to its output
    const source = gen.addOutputFileArg("source");
    gen.has_side_effects = true;                           // runs on every build

    const run = b.addSystemCommand(&.{ "sh", "run.sh" });
    run.addFileArg(source);                                // cached by the content of source
    const output = run.addOutputFileArg("output");

    b.getInstallStep().dependOn(&b.addInstallFile(output, "output").step);
}
```

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep source = {
        .command = (const char *[]){"sh", "gen.sh", "$out", nullptr},
        .outputs = (const char *[]){"source", nullptr},
        .always = true,
        .restat = true,
    };
    smith_step(b, &source);
    SmithStep output = {
        .command = (const char *[]){"sh", "run.sh", "$in", "$out", nullptr},
        .inputs = source.outputs,
        .outputs = (const char *[]){"output", nullptr},
    };
    smith_step(b, &output);
    smith_default(b, output.outputs);
}
```

**Verified:** make: the generator ran on every `make` (4 runs in 4 invocations), `run.sh` only after the system information changed (2 runs). ninja: with the phony `always` input, same counts (gen 4, run 2); the generator printed `[1/2]` each time, with `output` pruned when the content was unchanged. Without the `| always` input, ninja said `no work to do` and never reran the generator, so an edge with no inputs and an existing output is not "always". `always = true` in smith should emit that phony input (one shared `build smith_always: phony` edge). Zig 0.17.0: the snippet ran unchanged (output `zig-out/output`). Over five builds `gen.sh` ran 5 times (every build); `run.sh` ran twice (first build, and after `system1-data` changed); the builds in between showed `gen` as `success` and `run` as `cached`. A step with `has_side_effects` keeps a stable output path (a hash of its arguments, not of its outputs), so `gen.sh`'s own "write only if changed" works as `restat` does, and the file the next step reads keeps its bytes and path.
**How each handles it:** make: natively, with a `FORCE` target. ninja: with a workaround (the phony target with no inputs, plus `restat`). Zig: natively (`has_side_effects` on the generator; the next step is cached because the generated file keeps its path and bytes). smith: natively through `.always` plus `.restat`, emitted as that phony trick.
**What smith's API needs:** nothing new (`.always` and `.restat`: v1). `.always` adds an input on one shared `build
smith_always: phony` edge that has no inputs. `.always` is for steps; a task runs every time anyway.

### system2: rerun only if the output is missing or an environment variable changed

Scenario: `run.sh output` writes `$SYSTEM2_DATA` to `output`. It must rerun when `output` is missing or when `SYSTEM2_DATA` has a different value than at the last run, and only then.

**make** (GNU Make)
```make
output: env.stamp
	sh run.sh output

env.stamp: FORCE
	sh stamp.sh env.stamp

FORCE:
```
(`stamp.sh file` writes `$SYSTEM2_DATA` into the file only if it differs from what is there.)

**ninja**
```ninja
rule run
    command = SYSTEM2_DATA='a' sh run.sh $out
build output: run
```
(the value is part of the command line, which ninja records and compares; the build file is regenerated when the variable changes). The make-style alternative also works:
```ninja
rule stamp
    command = sh stamp.sh $out
    restat = 1
rule run
    command = sh run.sh $out
build always: phony
build env.stamp: stamp | always
build output: run | env.stamp
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const run = b.addSystemCommand(&.{ "sh", "run.sh" });
    const output = run.addOutputFileArg("output");
    // The configure result is cached, so an environment variable read here is not seen again unless the
    // cache is poisoned. Environment entries set on a Run step are part of its cache key.
    b.graph.poisonCache();
    const data = b.graph.environ_map.get("SYSTEM2_DATA") orelse "";
    run.setEnvironmentVariable("SYSTEM2_DATA", data);

    b.getInstallStep().dependOn(&b.addInstallFile(output, "output").step);
}
```
Changes from the 0.15 version: `std.process.getEnvVarOwned` is gone (the environment is `b.graph.environ_map`), and `b.graph.poisonCache()` is required: the configure result is cached, so without it a changed variable is never read again.

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *data = smith_env(b, "SYSTEM2_DATA", ""); // read on every smith build
    SmithStep run = {
        .command = (const char *[]){"env", smith_fmt(b, "SYSTEM2_DATA=%s", data), "sh", "run.sh", "$out", nullptr},
        .outputs = (const char *[]){"output", nullptr},
    };
    smith_step(b, &run);
    smith_default(b, run.outputs);
}
```

**Verified:** make with the stamp file: `make` built it; the same value again did nothing but the stamp check; `SYSTEM2_DATA=b make` reran `run.sh`; again with the same value did nothing; deleting `output` reran it (3 runs in all, output `b`). ninja stamp form: identical results (3 runs). ninja baked-in form: `build.ninja` with `SYSTEM2_DATA='a'` built once; a second run did nothing; after editing the file to `'b'` (simulating the regenerated manifest) ninja reran the step (2 runs in all, output `b`) — ninja compares the command line, so a changed variable in the command is enough. Not verified: nothing rereads the environment unless something reruns `build.c`; smith reruns it on every `smith build`, so `smith_env` is always current. Also the value ends up in `build.ninja` and `.ninja_log`, which matters if it is a secret. Zig 0.17.0: the original did not compile (`getEnvVarOwned`). The version above: unset, first run ran `run.sh` (output empty); `SYSTEM2_DATA=a zig build` reran (output `a`); again with `a` `cached`; `b` reran (output `b`); again `cached`. Deleting `zig-out/output` reinstalled it from the cache without rerunning `run.sh`. Deleting the cache's own copy (`.zig-cache/o/<hash>/output`) did not rerun it either: the Run step said `cached` and the install step failed with `FileNotFound`, so "rerun if the output is missing" holds only for the installed copy. Without `poisonCache` the change from `a` to `b` was not seen and `a` stayed. With it a warm `zig build` takes 0.12s, because the configurer reruns every time.
**How each handles it:** make: with a workaround (stamp file written only when the value differs). ninja: with a workaround (value baked into the command, or the stamp file with `restat`); no native tracking. Zig: with a workaround (read `environ_map` in `build.zig`, call `poisonCache` so the configuration is rerun, and the variable is part of the Run step's cache key; a deleted installed output is restored from the cache, not rebuilt). smith: with one new function, `smith_env`, and the value passed on the command line.
**What smith's API needs:** `smith_env` (v1):

```c
const char *smith_env(SmithBuild *b, const char *name, const char *fallback);
```
It returns the variable, or `fallback` when unset. `build.c` runs on every `smith build`, so a changed value changes the
command, and ninja reruns the step. A later cache would track the name and value, as it would a glob's result. An `environment` field on `SmithStep` is
not needed: `env NAME=value` in the command is hashed by ninja already.

### pool: limit one stage to 2 concurrent runs while the rest runs with -j8

**make** (GNU Make)
```make
# Makefile: no per-rule limit exists; run the stage in a sub-make with its own -j2
all: stage
stage:
	$(MAKE) -j2 -f stage.mk
# stage.mk
all: o1 o2 o3 o4
o%: i%
	./pool-run $< -- $@
```

**ninja**
```ninja
pool limit
  depth = 2
rule run
  command = ./pool-run $in -- $out
  pool = limit
build output1: run input1
build output2: run input2
build output3: run input3
build output4: run input4
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    // No pool concept. Step.max_rss is the closest tool: the build runner never lets the declared
    // max_rss of the steps running at once add up to more than its memory budget (--maxrss, default
    // total system memory). A claim of half the budget therefore means at most 2 such steps at once.
    const budget = std.process.totalSystemMemory() catch @panic("memory");
    inline for (.{ "1", "2", "3", "4" }) |n| {
        const run = b.addSystemCommand(&.{"./pool-run"});
        run.addFileArg(b.path("input" ++ n));
        run.addArg("--");
        const out = run.addOutputFileArg("output" ++ n);
        run.step.max_rss = budget / 2;
        b.getInstallStep().dependOn(&b.addInstallFile(out, "output" ++ n).step);
    }
}
```
There is no pool, but a step can declare an upper bound on its memory (`Step.max_rss`), and the build runner never lets the declared bounds of the steps running at once add up to more than its budget (`--maxrss`, default total system memory). Giving each step half of the budget lets at most 2 run at once. The first snippet in the 0.15 file (a `dependOn` chain) also compiles and gives depth 1.

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_pool(b, "limit", 2);
    const char *const *outputs = (const char *[]){nullptr};
    for (int i = 1; i <= 4; i++) {
        SmithStep run = {
            .command = (const char *[]){"./pool-run", "$in", "--", "$out", nullptr},
            .inputs = (const char *[]){smith_fmt(b, "input%d", i), nullptr},
            .outputs = (const char *[]){smith_fmt(b, "output%d", i), nullptr},
            .pool = "limit",
        };
        smith_step(b, &run);
        outputs = smith_cat(b, outputs, run.outputs, nullptr);
    }
    smith_default(b, outputs);
}
```

**Verified:** ninja with `-j8` and four 1-second jobs: outputs 1 and 2 started in the same second and finished together, then 3 and 4 started in the next second (never more than 2 at once). make -j8 ran all four at the same moment. The make workaround (a sub-make with `-j2`) did cap the stage at 2, but make prints "-jN forced in submake: disabling jobserver mode", so the sub-make no longer shares the parent's job budget. Zig 0.17.0 (`-j8`, four 1-second jobs): with no limit all four started in the same second; the `dependOn` chain ran them one after another (4 seconds); with the `max_rss` claim two started in one second and the other two in the next. Outputs are `zig-out/output1`..`output4`.
**How each handles it:** make: only with a workaround (a sub-make with its own `-j`, which loses the shared job count). ninja: natively (`pool`). Zig: with a workaround (no pool, but `max_rss` claims against the runner's memory budget give depth 2; the budget is global to the build and shared with every step that sets `max_rss`, and a claim above the budget fails the build; `dependOn` chaining gives depth 1). smith: natively, by passing through to ninja pools.
**What smith's API needs:** nothing new. `smith_pool` and `.pool` (v1) are enough.

### digest: do not rerun a step when its input is rewritten with identical contents

**make** (GNU Make)
```make
# No content checking. Workaround: depend on a hash file that is only rewritten when the hash changes.
output: input.hash
	sh run.sh input output
input.hash: FORCE
	@h=$$(shasum -a 256 input | cut -d' ' -f1); [ "$$(cat $@ 2>/dev/null)" = "$$h" ] || echo $$h > $@
FORCE:
```

**ninja**
```ninja
# Plain version: reruns on a same-content rewrite, with or without restat.
rule run
  command = sh run.sh $in $out
build output: run input

# Working version (what smith would generate for digest): a hash edge with restat,
# and the real step depends on the hash file, not on input.
rule digest
  command = sh digest.sh $in $out   # rewrites $out only if the hash differs
  restat = 1
rule run
  command = sh run.sh input $out
build input.hash: digest input
build output: run | input.hash
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    // `input` is named in a depfile the command writes, not as a file argument. Zig hashes the
    // contents of files it learns about from a depfile, so a same-content rewrite is a cache hit.
    const run = b.addSystemCommand(&.{ "sh", "-c", "sh run.sh \"$1\" \"$2\" && echo \"$2: $1\" > \"$3\"", "sh", "input" });
    const out = run.addOutputFileArg("output");
    _ = run.addDepFileOutputArg("output.d");
    run.addFileInput(b.path("run.sh"));
    b.getInstallStep().dependOn(&b.addInstallFile(out, "output").step);
}
```
`input` is not a file argument: it is named in a depfile the command writes, and Zig compares the contents of files named in a depfile. `run.sh` is the same script (`cp $1 $2`). The first run has no depfile yet, so `input` is only recorded after it. The wrapper is a shell one-liner; the original, with `addFileArg(b.path("input"))`, is described under Verified.

**smith build.c** (later)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep run = {
        .command = (const char *[]){"sh", "run.sh", "$in", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"output", nullptr},
        .content_hash = true, // rerun only if the contents of the inputs changed
    };
    smith_step(b, &run);
    smith_default(b, run.outputs);
}
```

**Verified:**
- ninja, `output` built from `input`, then `input` rewritten with the same bytes and a newer mtime: the step reran with and without `restat = 1` (2 runs each). `restat` only compares the step's own outputs after it ran, so it cannot stop the step itself.
- ninja, with a downstream edge (`output2` from `output`, `restat = 1` on the first): both edges reran (4 runs). The step copies its input with `cp`, which always touches the output, so `restat` saw a new mtime.
- ninja, same but the step writes `output` only when the bytes differ (write to a temp file, `cmp`, `mv` only if different): the step reran (it has to), the downstream edge did not (3 runs, "[1/2]"). This is the only thing `restat` helps with: pruning what comes after a step that already ran.
- ninja, hash-file version above: first build ran the step once; rewriting `input` with identical contents ran only the `digest` edge (`[1/2]`), the real step did not run; changing `input` to `v2` reran both; a plain `touch input` ran only `digest`. So content cutoff on inputs works in plain ninja with a generated stamp.
- make: not run. The make recipe is the standard hash-file workaround and has the same shape.
- Zig 0.17.0: the original (a plain file argument) does NOT cut off on content: a same-bytes rewrite (copy to a temp file and rename) reran, `touch` reran, `touch -t` to an old date reran, and changing the file back to `v1` reran again; worse, a content change made in place that kept the size, inode and modification time was not noticed (the step stayed `cached` and the output kept the old bytes). So a file argument is checked by size, inode and mtime, not by content. The depfile version: first build ran `run.sh` once; again `cached`; a same-bytes rewrite and a `touch` (after a 2s sleep) stayed `cached` (log 1 line); changing `input` to `v2` reran (log 2 lines, `zig-out/output` = `v2`).
**How each handles it:** make: not natively (workaround with a hash file and a phony `FORCE`, which hashes every build). ninja: not natively and `restat` does not help; the hash-file edge above is a working workaround. Zig: with a workaround. Not natively: a file given as an argument is checked by size, inode and mtime, so a same-bytes rewrite reruns (and a same-size in-place change with the same mtime is missed); a file the command names in a depfile is compared by content, which gives the cutoff. smith: not by passing through, but smith can generate the hash-file edges itself.
**What smith's API needs:** `.content_hash` (later), one field on `SmithStep`: "rerun only if the contents of the inputs
changed, not their timestamps". Proposed design and cost:
- For each input that is a file, smith emits `build .smith/hash/<path>.hash: smith_hash <path>` with `restat = 1`, and
  the step lists the `.hash` file as an implicit input instead of the file (the command line still names the real file,
  so `$in` is unchanged). The hash command is a subcommand of smith itself (`smith --smith-hash in out`), which writes
  `out` only if the hash differs, so there is no dependency on `shasum`.
- Cost: one extra tiny edge per input of a `content_hash` step (a process spawn plus hashing the file, only when its
  mtime changed, because ninja still compares mtimes first), a `.smith/hash/` folder, and doubled edge count for those
  steps. Steps without the field are unaffected, so by default nothing changes.
- Limits: dependencies found through `depfile` (for example headers) are still tracked by mtime, because ninja reads
  them itself. To cover headers, the hash edge would have to run after the compile, which is a larger design. The input
  must not also be listed directly as an input of the step, or its mtime brings the rerun back (smith must remove it).
- Alternative: smith's own executor later, keeping a hash per output in a small database (the way Shake does). That
  would cover depfile inputs too, but it means replacing ninja for those steps. The hash-edge version needs no executor.

### nofileout: rerun if and only if the input changed, with no output file

**make** (GNU Make)
```make
nofileout-run: .stamp
.stamp: input
	sh run.sh input && touch $@
```

**ninja**
```ninja
rule run
  command = sh run.sh $in && touch $out
build .smith/stamp/81b43ea1e0306b15: run input
build nofileout-run: phony .smith/stamp/81b43ea1e0306b15
default nofileout-run
```
This block was updated to the v1 decisions and not rerun. The stamp name is the one smith writes for the snippet
below (the FNV-1a 64 hash of its command and inputs).

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");
pub fn build(b: *std.Build) void {
    const run = b.addSystemCommand(&.{ "sh", "run.sh" });
    run.addFileArg(b.path("input"));
    run.expectExitCode(0); // a checked Run step is cached by its inputs even with no output file
    b.step("nofileout-run", "run when input changed").dependOn(&run.step);
}
```
`run.sh` is the same script (it appends `run` to `log`). `zig build nofileout-run` runs it. The step is cached by its inputs even though it has no output file; a checked Run step shows its standard error only when it fails. The script is named as a plain string, so editing `run.sh` does not rerun it (use `addFileInput`).

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep run = {
        .command = (const char *[]){"sh", "run.sh", "$in", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        // no outputs: smith adds a stamp and touches it after the command succeeds
    };
    smith_step(b, &run);
    smith_alias(b, "nofileout-run", run.outputs);
    smith_default(b, run.outputs);
}
```

**Verified:**
- ninja, stamp version: first build ran once, a second build printed "no work to do" (1 run), after changing `input` it ran again (2 runs). If the command fails, the stamp is not touched, so it runs again next build (checked with a failing command: 2 runs in 2 builds).
- ninja has no edge without an output: `build : run input` is a syntax error ("near here" at the colon).
- ninja, an edge with a made-up output name that the command never creates (`build task: run input`): it ran on every build (2 runs in 2 builds), so that is the "always" behaviour, not "when changed".
- make, stamp version: 2 runs over three builds (the nochange build said nothing to be done, the changed one ran).
- Zig 0.17.0 (three builds, the third after changing `input`): the original, a Run step with no output and nothing captured, ran every time (3 runs in 3 builds; the Run source treats a step with no output file as having side effects); `_ = run.captureStdOut();` did not compile (it takes an options struct), and `captureStdOut(.{})` gave 1, 1, 2 runs; `run.expectExitCode(0)` with no output file also gave 1, 1, 2 runs. A command that exits 1 failed the build each time and ran again each time (2 runs in 2 builds).
**How each handles it:** make: with a workaround (stamp file). ninja: with a workaround (stamp file; a command that never creates its declared output reruns every time). Zig: natively with one call (`expectExitCode(0)` makes a Run step with no output file cacheable by its inputs). The claim that an unchecked Run step with no outputs always runs is right; the documented `captureStdOut(.{})` also works but writes a stdout file. smith: with a workaround that smith does for the user, by creating the stamp.
**What smith's API needs:** `smith_step` (v1), with no new signature. Pin its meaning in the docs: a step with no
outputs gets a stamp, `.smith/stamp/<hash>`, and `&& touch <stamp>` ends its command, so it reruns only when its
inputs changed. A stamp is touched only on success (shown above). A task is different: it runs every time it is asked
for, like make's `.PHONY`, and it cannot have outputs. So this case is a step, not a task.

### noleftover: delete files left over from a previous build when they are no longer produced

**make** (GNU Make)
```make
# Make has no record of earlier outputs; bar.out stays.
OUT := foo.out baz.out
all: $(OUT)
%.out: %.in
	sh run.sh $< $@
# Workaround: a clean target, or keep a list file of outputs and delete the ones not in $(OUT).
```

**ninja**
```ninja
rule run
  command = sh run.sh $in $out
build foo.out: run foo.in
build baz.out: run baz.in
# bar.out was in the previous manifest. Afterwards: ninja -t cleandead (with smith: smith build -t cleandead)
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

// Edit this list between builds: the first build used { "foo", "bar" }, the second { "foo", "baz" }.
const names = [_][]const u8{ "foo", "baz" };

pub fn build(b: *std.Build) void {
    const outputs = b.addWriteFiles(); // a directory in the cache holding exactly the current outputs
    for (names) |name| {
        const run = b.addSystemCommand(&.{ "sh", "run.sh" });
        run.addFileArg(b.path(b.fmt("{s}.in", .{name})));
        const out = run.addOutputFileArg(b.fmt("{s}.out", .{name}));
        _ = outputs.addCopyFile(out, b.fmt("{s}.out", .{name}));
    }
    // Replace the install folder instead of adding to it, so a file no longer produced disappears.
    const publish = b.addSystemCommand(&.{ "sh", "-c", "rm -rf \"$2\" && mkdir -p \"$(dirname \"$2\")\" && cp -R \"$1\" \"$2\"", "sh" });
    publish.addDirectoryArg(outputs.getDirectory());
    publish.addArg("zig-out/out"); // assumes the default prefix
    publish.has_side_effects = true; // the destination is not tracked
    b.getInstallStep().dependOn(&publish.step);
}
```
To follow the case, `names` is edited between builds (first `foo` and `bar`, then `foo` and `baz`). The plain version, with an `addInstallFile` per name straight into `zig-out/`, leaves `zig-out/bar.out` behind. `run.sh` is the same script (`cp $1 $2` plus a log line).

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *outputs = (const char *[]){nullptr};
    for (const char *const *in = smith_glob(b, (const char *[]){"*.in", nullptr}); *in; in++) {
        // adding baz.in or removing bar.in changes the manifest on the next smith build
        SmithStep run = {
            .command = (const char *[]){"sh", "run.sh", "$in", "$out", nullptr},
            .inputs = (const char *[]){*in, nullptr},
            .outputs = (const char *[]){smith_replace_extension(b, *in, ".out"), nullptr},
        };
        smith_step(b, &run);
        outputs = smith_cat(b, outputs, run.outputs, nullptr);
    }
    smith_default(b, outputs);
}
```

**Verified:**
- ninja: built `foo.out` and `bar.out`; then removed `bar.in`, added `baz.in`, and switched the manifest to foo and baz. A normal `ninja` built `baz.out` and left `bar.out` in place. `ninja -t cleandead -n` reported "Cleaning... 1 files." and the real `ninja -t cleandead` removed `bar.out` (the listing afterwards showed only foo.out and baz.out; its message said "Cleaning... 0 files", which does not match the dry run, so treat the count as unreliable and check the files). Running it again was a no-op.
- ninja: if `.ninja_log` is deleted, `cleandead` knows nothing and leaves old outputs alone (an `old.out` survived).
- make: not run; it keeps no record of outputs, so `bar.out` stays by construction.
- Zig 0.17.0: the plain version built `zig-out/foo.out` and `zig-out/bar.out`; after removing `bar.in`, adding `baz.in` and switching the list, `zig-out` held `bar.out`, `baz.out` and `foo.out`. `zig build uninstall` is listed by `zig build -l` ("Remove build artifacts from prefix path") but removed nothing. The workaround above: `zig-out/out/` held `bar.out foo.out`, then after the switch only `baz.out foo.out`; the following build re-ran the `publish` step (it has side effects) while both generator steps stayed `cached`.
**How each handles it:** make: not at all. ninja: with a workaround (`ninja -t cleandead`, which only knows files recorded in `.ninja_log`). Zig: not natively (`zig-out` keeps stale files and the listed `uninstall` step removes nothing); with a workaround, a Run step that replaces the install folder from a cache folder holding exactly the current outputs. smith: with a workaround, the same one: the user runs `smith build -t cleandead`, which is ninja's tool passed through.
**What smith's API needs:** nothing new (`smith_glob` and `smith_replace_extension`: v1). smith does not run
`cleandead` by itself. make never deletes leftover outputs, so smith does not either, and an automatic run could delete a
file that smith used to build and the project now keeps as a hand-written source at the same path.
`smith build -t cleandead` is there when wanted. The glob is read on every `smith build`, so adding `baz.in` or removing
`bar.in` changes the manifest on the next call.

### secondary: keep an auxiliary file out of the rebuild decision

**make** (GNU Make)
```make
output: secondary
	sh rr.sh secondary -- output
secondary: input
	sh rr.sh input -- secondary
.SECONDARY: secondary
```

**ninja**
```ninja
rule run
  command = sh rr.sh $in -- $out
rule run2
  command = sh rr.sh secondary -- $out
build secondary: run input
build output: run2 | input || secondary
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");
pub fn build(b: *std.Build) void {
    const gen = b.addSystemCommand(&.{ "sh", "rr.sh" });
    gen.addFileArg(b.path("input"));
    gen.addArg("--");
    const secondary = gen.addOutputFileArg("secondary"); // lives in the cache
    const run = b.addSystemCommand(&.{ "sh", "rr.sh" });
    run.addFileArg(secondary);
    run.addArg("--");
    const out = run.addOutputFileArg("output");
    b.getInstallStep().dependOn(&b.addInstallFile(out, "output").step); // secondary is not installed
}
```

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep secondary = {
        .command = (const char *[]){"sh", "rr.sh", "$in", "--", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"secondary", nullptr},
    };
    smith_step(b, &secondary);
    SmithStep output = {
        .command = (const char *[]){"sh", "rr.sh", "secondary", "--", "$out", nullptr},
        .implicit_inputs = (const char *[]){"input", nullptr},
        .order_only_inputs = secondary.outputs,
        .outputs = (const char *[]){"output", nullptr},
    };
    smith_step(b, &output);
    smith_default(b, output.outputs);
}
```

**Verified:** scenario: build, build again, delete `secondary`, build, then change `input`.
- make with `.SECONDARY`: first build ran both steps; second and "delete secondary" builds ran nothing (make said `output` is up to date, and `secondary` was not recreated); changing `input` ran both. Matches the case exactly.
- ninja with `secondary` as an ordinary input of `output`: deleting `secondary` reran both steps (the case wants no rerun of `output`).
- ninja with `input` as an implicit input and `secondary` order-only (the snippet above): first build ran both; second ran nothing; after deleting `secondary` only the secondary step ran (`output` did not rerun); changing `input` ran both, in order. The difference from make: ninja recreates `secondary` when it is missing, make leaves it missing.
- Zig 0.17.0 (output `zig-out/output`; the snippet ran unchanged): first build ran both steps (log `secondary`, `output`); again both `cached`; deleting `zig-out/output` only reinstalled it; deleting the cache's copy of `secondary` (`.zig-cache/o/<hash>/secondary`, the only place it exists) made the build FAIL with `checking cache failed: file_open FileNotFound`, with nothing rerun, until `input` changed, which ran both steps and recovered. When `secondary` was also installed, deleting `zig-out/secondary` reinstalled it from the cache and reran nothing.
**How each handles it:** make: natively (`.SECONDARY`). ninja: with a workaround (implicit input on the source plus order-only on the auxiliary file; the auxiliary file is rebuilt if missing but does not rerun `output`). Zig: natively as far as the decision goes (generated files live in the cache and are not installed, and nothing reruns when an installed copy is deleted); but the cache's copy of `secondary` cannot be deleted without breaking the build until `input` changes, unlike make (leaves it missing) and ninja (recreates it). smith: with the same workaround via existing fields.
**What smith's API needs:** nothing new (`.implicit_inputs` and `.order_only_inputs`: v1). Worth a documented recipe,
since the intuitive form (output takes `secondary` in `inputs`) behaves differently.

### intermediate: an auxiliary file that is deleted at the end and not recreated while the input is unchanged

**make** (GNU Make)
```make
output: intermediate
	sh rr.sh intermediate -- output
intermediate: input
	sh rr.sh input -- intermediate
.INTERMEDIATE: intermediate
```

**ninja**
```ninja
# One edge makes the intermediate file inside its own command, so ninja never sees it.
rule smith_cmd
  command = sh -c 'sh rr.sh "$$1" -- "$$2.tmp" && sh rr.sh "$$2.tmp" -- "$$2" && rm "$$2.tmp"' sh $in $out
build output: smith_cmd input
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");
// Same as secondary: the intermediate is an output of a Run step that lives in the cache and is never installed.
pub fn build(b: *std.Build) void {
    const gen = b.addSystemCommand(&.{ "sh", "rr.sh" });
    gen.addFileArg(b.path("input"));
    gen.addArg("--");
    const intermediate = gen.addOutputFileArg("intermediate");
    const run = b.addSystemCommand(&.{ "sh", "rr.sh" });
    run.addFileArg(intermediate);
    run.addArg("--");
    const out = run.addOutputFileArg("output");
    b.getInstallStep().dependOn(&b.addInstallFile(out, "output").step);
}
```

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep output = {
        .command = (const char *[]){"sh", "-c",
                                    "sh rr.sh \"$1\" -- \"$2.tmp\" && sh rr.sh \"$2.tmp\" -- \"$2\" && rm \"$2.tmp\"",
                                    "sh", "$in", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"output", nullptr},
    };
    smith_step(b, &output);
    smith_default(b, output.outputs);
}
```

**Verified:** build, build again, then change `input`; the log shows each run.
- make with `.INTERMEDIATE`: first build ran both steps and removed `intermediate`; second build said `output` is up to date and ran nothing (make does not recreate it); the change ran both and removed it again.
- ninja, two edges with the second doing `rm` of the intermediate: it ran both steps again on every build (the missing file makes the first edge dirty), so the file is "recreated in subsequent builds". The case fails.
- ninja, one edge (command above, with `$$` as smith would escape it): first build ran the temp step and the final step and left only `output`; second build "no work to do"; change ran both again; no leftover `output.tmp`. The quoted `$1`/`$2` form was also run, and gave the same result. In this form `$out` appears only as a whole word, which matches the "only whole-word `$in` and `$out` expand" rule.
- Zig 0.17.0 (the snippet ran unchanged): build, build again, change `input`: the log shows `intermediate`, `output`, then nothing, then `intermediate`, `output` again. The tree held only `zig-out/output`; the intermediate stays in `.zig-cache/o/<hash>/intermediate` (one folder per version, not deleted at the end).
**How each handles it:** make: natively (`.INTERMEDIATE`). ninja: with a workaround (merge the steps into one edge; the intermediate is invisible to ninja). Zig: natively (the intermediate is a cache file, never installed and not recreated while `input` is unchanged; it is not deleted, old versions stay in `.zig-cache`). smith: with a workaround (one step, or a C-function step with `run`, which can create and remove a temp file without shell quoting).
**What smith's API needs:** nothing new (v1). A step written as a C function (`.run`, later) would create and remove the
temp file without the shell quoting. A helper for a temp path (`smith_run_temp`) was proposed; no case needs it yet, so
it was dropped.

### variants: the same program built twice, plain and with `-fsanitize=address,undefined`, into two folders, from one description

**make** (GNU Make)
```make
CC := clang
SAN := -fsanitize=address,undefined -fno-sanitize-recover=all

all: build/plain/app build/sanitize/app

# One template, instantiated per variant: $(1) is the folder, $(2) the extra flags.
define variant
build/$(1)/%.o: %.c
	@mkdir -p $$(@D)
	$$(CC) $(2) -MMD -MP -c $$< -o $$@
build/$(1)/app: build/$(1)/main.o build/$(1)/util.o
	$$(CC) $(2) $$^ -o $$@
-include build/$(1)/main.d build/$(1)/util.d
endef
$(eval $(call variant,plain,))
$(eval $(call variant,sanitize,$(SAN)))
```

**ninja**
```ninja
ninja_required_version = 1.10

rule cc
  command = clang $flags -MMD -MF $out.d -c $in -o $out
  description = cc $out
  depfile = $out.d
  deps = gcc

rule link
  command = clang $flags $in -o $out
  description = link $out

build build/plain/main.o: cc main.c
build build/plain/util.o: cc util.c
build build/plain/app: link build/plain/main.o build/plain/util.o

build build/sanitize/main.o: cc main.c
  flags = -fsanitize=address,undefined -fno-sanitize-recover=all
build build/sanitize/util.o: cc util.c
  flags = -fsanitize=address,undefined -fno-sanitize-recover=all
build build/sanitize/app: link build/sanitize/main.o build/sanitize/util.o
  flags = -fsanitize=address,undefined -fno-sanitize-recover=all

default build/plain/app build/sanitize/app
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

fn addApp(
    b: *std.Build,
    folder: []const u8,
    sanitize_c: std.zig.SanitizeC,
    flags: []const []const u8,
) *std.Build.Step.Compile {
    const module = b.createModule(.{
        .target = b.graph.host,
        .optimize = .Debug,
        .link_libc = true,
        .sanitize_c = sanitize_c, // Debug turns UBSan on by default, so the plain variant says .off
    });
    module.addCSourceFiles(.{ .files = &.{ "main.c", "util.c" }, .flags = flags });
    const exe = b.addExecutable(.{ .name = "app", .root_module = module });
    // Each variant installs into its own folder under zig-out/.
    const install = b.addInstallArtifact(exe, .{ .dest_dir = .{ .override = .{ .custom = folder } } });
    b.getInstallStep().dependOn(&install.step);
    return exe;
}

pub fn build(b: *std.Build) void {
    _ = addApp(b, "plain", .off, &.{});
    const sanitize = addApp(b, "sanitize", .off, &.{ "-fsanitize=address,undefined", "-fno-sanitize-recover=all" });
    // zig ships no AddressSanitizer runtime (its bundled clang compiles the flag, then the link fails with
    // undefined ___asan_init), so link the one from an installed LLVM. It is a path on this machine.
    sanitize.root_module.addObjectFile(.{
        .cwd_relative = "/opt/homebrew/opt/llvm/lib/clang/23/lib/darwin/libclang_rt.asan_osx_dynamic.dylib",
    });
}
```
Changes from the 0.15 version: `sanitize_c` is set explicitly (Debug turns UBSan on for C by default, which would put UBSan into the plain variant too), and the AddressSanitizer runtime is linked by hand from an installed LLVM. The path is Homebrew LLVM 23's runtime; Zig's own clang here is 22.1.8 and the mismatch did not matter.

**smith build.c** (v1)
```c
#include <smith/build.h>

static const char *const *program(SmithBuild *b, const char *folder, const char *const *flags) {
    const char *const names[] = {"main", "util", nullptr};
    const char *const *objects = (const char *[]){nullptr};
    for (const char *const *name = names; *name; name++) {
        const char *object = smith_fmt(b, "build/%s/%s.o", folder, *name);
        const char *depfile = smith_fmt(b, "%s.d", object);
        SmithStep cc = {
            .command = smith_cat(b, (const char *[]){"clang", nullptr}, flags,
                                 (const char *[]){"-MMD", "-MF", depfile, "-c", "$in", "-o", "$out", nullptr}, nullptr),
            .inputs = (const char *[]){smith_fmt(b, "%s.c", *name), nullptr},
            .outputs = (const char *[]){object, nullptr},
            .depfile = depfile,
            .description = smith_fmt(b, "cc %s", object),
        };
        smith_step(b, &cc);
        objects = smith_cat(b, objects, cc.outputs, nullptr);
    }
    SmithStep link = {
        .command = smith_cat(b, (const char *[]){"clang", nullptr}, flags,
                             (const char *[]){"$in", "-o", "$out", nullptr}, nullptr),
        .inputs = objects,
        .outputs = (const char *[]){smith_fmt(b, "build/%s/app", folder), nullptr},
        .description = smith_fmt(b, "link build/%s/app", folder),
    };
    smith_step(b, &link);
    return link.outputs;
}

void build(SmithBuild *b) {
    const char *const *plain = program(b, "plain", (const char *[]){nullptr});
    const char *const *sanitize =
        program(b, "sanitize", (const char *[]){"-fsanitize=address,undefined", "-fno-sanitize-recover=all", nullptr});
    smith_default(b, smith_cat(b, plain, sanitize, nullptr));
}
```

**Verified:** make: clean build ran 6 compiles/links, both `app`s print 3, `touch util.c` rebuilt `util.o` and the link of both variants and nothing else. A first draft built only the plain variant because the template's first rule became the default goal; the `all:` line must come first. ninja: hand-written file above, clean build ran 6 edges, both programs print 3; `nm` shows 10 `asan` symbols in the sanitized binary and 0 in the plain one; `touch util.c` and a one-character edit each rebuilt only `util.o` and the two links; a second run did nothing. Nothing from `build.c` or `build.zig` was run. Zig 0.17.0 (outputs `zig-out/plain/app` and `zig-out/sanitize/app`): the original compiled both variants but the one with `-fsanitize=address` failed to link with 4 errors (`undefined symbol: ___asan_init`, `___asan_register_image_globals`, `___asan_unregister_image_globals`, `___asan_version_mismatch_check_v8`); with `undefined` alone it linked, but both binaries then contained UBSan symbols (69 in each by `nm`) because Debug enables it by default. The version above: both build, both print 3, `nm` shows 0 sanitizer symbols in `plain` and 14 asan and 2 ubsan in `sanitize`; a separate test program with a heap overflow linked the same way aborted with an AddressSanitizer report. Changing `util.c` reran both compile steps, and a second run was `cached`. (A second try confirmed the runtime is the only thing missing.)
**How each handles it:** make: with a `define`/`$(call)`/`$(eval)` template, and the `$$` doubling is easy to get wrong. ninja: natively, with per-edge `flags` variables, but the repetition is written out by the generator. Zig: natively by a function called twice; the claim that Zig's clang has no ASan runtime is right (the link fails), but on macOS the runtime dylib of an installed LLVM can be added with `addObjectFile`, which worked; `sanitize_c = .off` is needed for the plain variant. smith: natively, a C function called twice, same as Zig.
**What smith's API needs:** nothing new (`smith_fmt` and `smith_cat`: v1). `program()` is an ordinary C function over
`SmithBuild *`; the variant is a parameter and every output path derives from `folder`. The generator emits `deps = gcc`
with `depfile = $out.d`, as in the ninja above, so ninja folds the `.d` into `.ninja_deps`.

### tool: build a small C program, run it to write a file, then `#embed` that file in a later compile

main.c is `static const unsigned char data[] = {` / `#embed "build/data.bin"` / `};` and bake.c writes four bytes (`i * 10 + BASE`) to `argv[1]`.

**make** (GNU Make)
```make
CC := /opt/homebrew/opt/llvm/bin/clang

all: build/app
build:
	mkdir -p $@
build/bake: bake.c | build
	$(CC) -DBASE=1 $< -o $@
build/data.bin: build/bake
	./build/bake $@
build/main.o: main.c build/data.bin
	$(CC) -std=c23 -MMD -MP -c $< -o $@
build/app: build/main.o
	$(CC) $^ -o $@
-include build/main.d
```

**ninja**
```ninja
rule cc
  command = /opt/homebrew/opt/llvm/bin/clang $flags -c $in -o $out
  description = cc $out
rule cc_dep
  command = /opt/homebrew/opt/llvm/bin/clang $flags -MMD -MF $out.d -c $in -o $out
  depfile = $out.d
  deps = gcc
  description = cc $out
rule link
  command = /opt/homebrew/opt/llvm/bin/clang $in -o $out
  description = link $out
rule run
  command = $in $out
  description = bake $out

build build/bake.o: cc bake.c
  flags = -DBASE=1
build build/bake: link build/bake.o
build build/data.bin: run build/bake
# data.bin is an implicit input (after |). The depfile cannot do this job: see Verified.
build build/main.o: cc_dep main.c | build/data.bin
  flags = -std=c23
build build/app: link build/main.o
default build/app
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const host = b.graph.host;

    const bake = b.addExecutable(.{
        .name = "bake",
        .root_module = b.createModule(.{ .target = host, .link_libc = true }),
    });
    bake.root_module.addCSourceFile(.{ .file = b.path("bake.c"), .flags = &.{"-DBASE=1"} });

    const run_bake = b.addRunArtifact(bake);
    const data = run_bake.addOutputFileArg("data.bin"); // lives in the cache

    // clang writes no depfile when #embed finds its file through -I, and zig requires one, so the file
    // has to be where main.c names it: copy it to build/data.bin in the source tree before compiling.
    const update = b.addUpdateSourceFiles();
    update.addCopyFileToSource(data, "build/data.bin");

    const app = b.addExecutable(.{
        .name = "app",
        .root_module = b.createModule(.{ .target = host, .link_libc = true }),
    });
    app.root_module.addCSourceFile(.{ .file = b.path("main.c"), .flags = &.{"-std=c23"} });
    app.step.dependOn(&update.step);
    b.installArtifact(app);
}
```
`main.c` is unchanged (`#embed "build/data.bin"`). The generated file is copied into the source tree because clang writes no depfile when `#embed` finds its file through an include path, and Zig treats a missing depfile as an error; `build/data.bin` is a generated file and should be ignored by version control.

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep bake_o = {
        .command = (const char *[]){"clang", "-DBASE=1", "-c", "$in", "-o", "$out", nullptr},
        .inputs = (const char *[]){"bake.c", nullptr},
        .outputs = (const char *[]){"build/bake.o", nullptr},
    };
    smith_step(b, &bake_o);
    SmithStep bake = {
        .command = (const char *[]){"clang", "$in", "-o", "$out", nullptr},
        .inputs = bake_o.outputs,
        .outputs = (const char *[]){"build/bake", nullptr},
    };
    smith_step(b, &bake);
    // Runs the program we just built. $in is the program, $out the file.
    SmithStep data = {
        .command = (const char *[]){"$in", "$out", nullptr},
        .inputs = bake.outputs,
        .outputs = (const char *[]){"build/data.bin", nullptr},
        .restat = true, // pairs with a baker that leaves an unchanged file alone
        .description = "bake build/data.bin",
    };
    smith_step(b, &data);
    SmithStep main_o = {
        .command =
            (const char *[]){"clang", "-std=c23", "-MMD", "-MF", "build/main.o.d", "-c", "$in", "-o", "$out", nullptr},
        .inputs = (const char *[]){"main.c", nullptr},
        .implicit_inputs = data.outputs, // required, not just the depfile
        .outputs = (const char *[]){"build/main.o", nullptr},
        .depfile = "build/main.o.d",
    };
    smith_step(b, &main_o);
    SmithStep app = {
        .command = (const char *[]){"clang", "$in", "-o", "$out", nullptr},
        .inputs = main_o.outputs,
        .outputs = (const char *[]){"build/app", nullptr},
    };
    smith_step(b, &app);
    smith_default(b, app.outputs);
}
```
(The ninja above names Homebrew LLVM 23's clang, because `#embed` needs it; here the program is written `clang`.)

**Verified:** make: clean build ran bake, then data.bin, then main.o, then app; app prints `4 31`. `touch bake.c` reran all four steps. ninja: the file above, clean build ran 5 edges in that order, app prints `4 31`; `touch bake.c` reran all 5; changing `-DBASE=1` to `-DBASE=5` in the ninja file reran all 5 and the app printed `4 35`; `touch main.c` reran only main.o and app. Three results matter. (1) Clang 23.1.3 with `-MMD -MF` and a missing `#embed` file exits 0, writes a .o and writes no .d file (without `-MMD` it is a fatal error). So with the implicit input removed, a clean build "succeeded" with only 2 edges, bake never ran and the object had no data. The depfile cannot be the only link to a generated file; `| build/data.bin` is required. (2) The `-MMD` .d for a build that has data.bin does list `build/data.bin`, so later edits are tracked twice. (3) With `restat = 1` on the bake rule and a baker that rewrites only when the bytes differ, `touch bake.c` ran `bake.o`, `bake`, and `data.bin` (3 edges) and skipped main.o and the link. Plain make has no equivalent, and make does not rerun anything when `-DBASE` changes in the Makefile. Zig 0.17.0 (output `zig-out/bin/app`): the original (`#embed "data.bin"` with `addIncludePath(data.dirname())`) compiled `bake` and ran it but failed on the app with `main.c:1:1: error: failed to read 'tmp/<id>-main.o.d': FileNotFound`; the same happens with plain `clang -std=c23 -I inc -c b.c -MD -MF b.d` (Apple's, Homebrew's 23): exit 0 and no `.d` file, while an `#embed` of a file in the current folder does write one. With the copy: `zig build` printed `4 31`; again all `cached`; `touch bake.c` recompiled and reran `bake` and recopied, but the app compile stayed `cached` (the bytes of `data.bin` were the same, and depfile files are compared by content); changing `-DBASE=1` to `-DBASE=5` reran everything and printed `4 35`; `touch main.c` reran only the app compile; changing back to `BASE=1` reran the app compile and printed `4 31`.
**How each handles it:** make: yes, as a plain rule, but no rerun on a flag change and no cutoff. ninja: yes, with `|` and `restat`, and reruns on a changed command line. Zig: natively with a workaround. `addRunArtifact` plus `addOutputFileArg` works, but the output is in the cache and `#embed` through the include path fails (no depfile from clang), so it is copied to `build/data.bin` with `addUpdateSourceFiles`; the embedded file is compared by content, so an unchanged `data.bin` does not recompile `main.c`. smith: natively with `$in` as a program; needs `implicit_inputs`, which exists.
**What smith's API needs:** nothing new (v1). Two conventions to write down: a generated file read by `#embed` or
`#include` must be in `implicit_inputs` of the compiling step (the depfile is not enough on the first build, and with
`#embed` missing a file clang can silently succeed), and `restat` only helps if the program leaves an unchanged output
untouched.

### options: a task that takes values from the command line (`make test TEST=name SEED=42`), empty when unset, and a changed value reruns the task

tests.c prints its arguments, so the output shows what was passed.

**make** (GNU Make)
```make
build/tests: tests.c
	@mkdir -p build
	clang $< -o $@

.PHONY: test
test: build/tests
	./build/tests $(if $(TEST),--test '$(TEST)') $(if $(SEED),--seed '$(SEED)')
```

**ninja**
```ninja
# What smith writes for: smith build test -DTEST=name -DSEED=42
rule cc
  command = clang $in -o $out
rule task
  command = $cmd
  pool = console
build build/tests: cc tests.c
build .smith/task/test: task build/tests
  cmd = ./build/tests --test name --seed 42
build test: phony .smith/task/test
default build/tests
# For: smith build test        the same file with   cmd = ./build/tests
# For: smith build test -DSEED=7                    cmd = ./build/tests --seed 7
```
This block was updated to the v1 decisions (the task edge and the default) and not rerun.

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const test_name = b.option([]const u8, "TEST", "run the test with this name");
    const seed = b.option([]const u8, "SEED", "run each property test on this seed alone");

    const tests = b.addExecutable(.{
        .name = "tests",
        .root_module = b.createModule(.{ .target = b.graph.host, .link_libc = true }),
    });
    tests.root_module.addCSourceFile(.{ .file = b.path("tests.c") });

    const run = b.addRunArtifact(tests);
    if (test_name) |name| run.addArgs(&.{ "--test", name });
    if (seed) |value| run.addArgs(&.{ "--seed", value });
    // A Run step with no output file counts as having side effects and runs on every build, so nothing more is needed.
    b.step("test", "run the tests").dependOn(&run.step);
}
```
The 0.15 version had `run.has_side_effects = true` with the comment that an unchanged run would otherwise be a cache hit. In 0.17 that is not so: a Run step with no output file always runs, so the line was dropped.

**smith build.c** (v1)
```c
#include <smith/build.h>

void build(SmithBuild *b) {
    const char *test = smith_option_value(b, "TEST", "", "run the test with this name");
    const char *seed = smith_option_value(b, "SEED", "", "run each property test on this seed alone");

    SmithStep tests = {
        .command = (const char *[]){"clang", "$in", "-o", "$out", nullptr},
        .inputs = (const char *[]){"tests.c", nullptr},
        .outputs = (const char *[]){"build/tests", nullptr},
    };
    smith_step(b, &tests);
    SmithStep run = {
        .command = smith_cat(b, (const char *[]){"$in", nullptr},
                             smith_when(b, test[0] != '\0', (const char *[]){"--test", test, nullptr}),
                             smith_when(b, seed[0] != '\0', (const char *[]){"--seed", seed, nullptr}), nullptr),
        .inputs = tests.outputs,
        .pool = "console",
    };
    smith_task(b, "test", &run);
    smith_default(b, tests.outputs);
}
```
(A task runs every time it is asked for, so `smith build test` reruns the tests with no `.always`, as in the ninja
above. The default builds `build/tests` and does not run the tests.)

**Verified:** make: `make test` ran `./build/tests` with no arguments (printing `tests:`), again with `TEST=name SEED=42` printed `--test name --seed 42`, with only `SEED=7` printed `--seed 7`, and a plain `make test` afterwards printed `tests:` again. Each run reran the test and did not recompile. ninja: five runs of `ninja -f <file> test` over files for no options, no options, both options, `SEED` only, no options: the first built `build/tests` and ran; every later run ran the task alone with exactly the argument words of its file (nothing for unset), including the second identical run, because `test` is an edge whose output is never created. Setting a value in the command line changes the command, which is the only thing that differs between runs. A folder named `test` did not stop the task from running in ninja 1.13.2 (tried), but I did not establish that it never matters; a safer form, which also worked, is an edge with output `task/test` plus `build test: phony task/test`. smith writes that form, with the output under `.smith/task/`. Zig 0.17.0 (the snippet ran unchanged, then without the `has_side_effects` line): `zig build test` printed `tests:`; `zig build test -DTEST=name -DSEED=42` printed `tests: --test name --seed 42`; `-DSEED=7` printed `tests: --seed 7`; the same again and a plain `zig build test` each ran the test again (`success`, never `cached`); `tests` compiled once (`cached` afterwards). `zig build -h` lists `-DTEST=[string]` and `-DSEED=[string]` with the descriptions. Without the `has_side_effects` line the behaviour was identical. (In a shell the two options must be separate words.)
**How each handles it:** make: natively; variables come from the command line and `$(if ...)` omits empty ones. ninja: not at all by itself; a program must write the command line with the values in it (smith does this by regenerating the file). Zig: natively with `b.option` and `-DTEST=name`; the run happens every time because a Run step with no output file counts as having side effects (no extra field needed). smith: natively with `smith_option_value`; `smith_when` is a helper on top.
**What smith's API needs:** nothing new beyond `smith_option_value`, `smith_task`, `smith_when` and `smith_cat` (v1).
Five behaviours to specify, none a signature:
1. An empty fallback must produce no words, which `smith_when` provides. Passing `""` through as an argument would make
   `--test ''`.
2. A `-D` value lasts for one `smith build` call, like make's. The build program runs on every `smith build` and gets
   the `-D` arguments of that call, so the manifest always matches them.
3. A changed value reruns only what uses it: only the option's use sites differ, so ninja reruns exactly the steps whose
   command text changed.
4. A task runs every time it is asked for, so no field is needed to make `smith build test` rerun the tests. A task
   cannot have `.always`.
5. A `-D` that `build.c` never declared is an error that lists the declared options with their descriptions.

### function: a step written as a C function, here `check-libc`: fail if an object uses a libc function outside an allowed list

Source: game's `check-libc` in its Makefile. For the test, base.c uses `memcpy` and `sqrtf`, game.c defines `game_run`; the failing variant of game.c adds a call to `puts`. (Apple clang adds `__memcpy_chk` for `memcpy` at -O0, so that name is in the allowed list, as in the real one.)

**make** (GNU Make)
```make
CC := clang
NM := nm
C_LAYERS := base game
LIBC_ALLOWED := memcpy memmove memset __memcpy_chk bzero sqrtf fabsf __stack_chk_guard __stack_chk_fail

build/%.o: %.c
	@mkdir -p build
	$(CC) -O0 -c $< -o $@

# Every symbol a layer uses without defining is one of ours or an allowed C library function.
.PHONY: check-libc
check-libc: $(C_LAYERS:%=build/%.o)
	@ours=$$(for layer in $(C_LAYERS); do $(NM) -gUj build/$$layer.o; done); fail=0; \
	for layer in $(C_LAYERS); do \
		for symbol in $$($(NM) -uj build/$$layer.o); do \
			name=$${symbol#_}; ok=0; \
			for known in $$ours; do if [ "$$symbol" = "$$known" ]; then ok=1; break; fi; done; \
			for known in $(LIBC_ALLOWED); do if [ "$$name" = "$$known" ]; then ok=1; break; fi; done; \
			if [ $$ok = 0 ]; then echo "build/$$layer.o uses $$name from the C library, which is not allowed"; fail=1; fi; \
		done; \
	done; \
	exit $$fail
```

**ninja**
```ninja
nm = nm
allowed = memcpy memmove memset __memcpy_chk bzero sqrtf fabsf __stack_chk_guard __stack_chk_fail

rule cc
  command = clang -O0 -c $in -o $out

# $$ is ninja's escape for a literal $; the script is the Makefile's, in one command.
rule check_libc
  command = ours=$$(for o in $in; do $nm -gUj $$o; done); fail=0; $
    for o in $in; do for symbol in $$($nm -uj $$o); do $
      name=$${symbol#_}; ok=0; $
      for known in $$ours; do if [ "$$symbol" = "$$known" ]; then ok=1; break; fi; done; $
      for known in $allowed; do if [ "$$name" = "$$known" ]; then ok=1; break; fi; done; $
      if [ $$ok = 0 ]; then echo "$$o uses $$name from the C library, which is not allowed"; fail=1; fi; $
    done; done; $
    test $$fail = 0 && touch $out
  description = check-libc

build build/base.o: cc base.c
build build/game.o: cc game.c
build build/check-libc.stamp: check_libc build/base.o build/game.o
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

// A step written in Zig (a custom Step with its own make function) no longer exists: the build runner has a fixed set of
// step kinds. A Run step with a checked exit code is the replacement; it is cached by its inputs and has no output file.
pub fn build(b: *std.Build) void {
    const check = b.addSystemCommand(&.{ "sh", "check-libc.sh" });
    check.addFileInput(b.path("check-libc.sh"));
    check.expectExitCode(0);
    for ([_][]const u8{ "base", "game" }) |name| {
        const object = b.addObject(.{
            .name = name,
            .root_module = b.createModule(.{ .target = b.graph.host, .optimize = .Debug, .link_libc = true }),
        });
        object.root_module.addCSourceFile(.{ .file = b.path(b.fmt("{s}.c", .{name})) });
        check.addFileArg(object.getEmittedBin());
    }
    b.step("check-libc", "objects use only allowed libc functions").dependOn(&check.step);
}
```
`check-libc.sh` is the make recipe as a script that takes the object files as arguments; it prints each failure to standard error (a checked Run step does not show standard output):

```sh
allowed="memcpy memmove memset __memcpy_chk bzero sqrtf fabsf __stack_chk_guard __stack_chk_fail"
ours=$(for o in "$@"; do nm -gUj "$o"; done)
fail=0
for o in "$@"; do
  for symbol in $(nm -uj "$o"); do
    name=${symbol#_}; ok=0
    for known in $ours; do if [ "$symbol" = "$known" ]; then ok=1; break; fi; done
    for known in $allowed; do if [ "$name" = "$known" ]; then ok=1; break; fi; done
    if [ $ok = 0 ]; then echo "$o uses $name from the C library, which is not allowed" >&2; fail=1; fi
  done
done
exit $fail
```

**smith build.c** (later)
```c
#include <smith/build.h>
#include <stdio.h>
#include <string.h>

static bool in_list(const char *const *list, const char *string) {
    for (; *list; list++) {
        if (strcmp(*list, string) == 0) return true;
    }
    return false;
}

// Runs inside the build program, not through a shell. run->b is the SmithBuild, so the list helpers work here too.
static int check_libc(SmithRun *run) {
    const char *const *ours = (const char *[]){nullptr};
    for (const char *const *object = run->inputs; *object; object++) {
        const char *const *defined = smith_run_lines(run, (const char *[]){"nm", "-gUj", *object, nullptr});
        if (defined == nullptr) return 1; // smith_run_lines already printed why
        ours = smith_cat(run->b, ours, defined, nullptr);
    }
    int failed = 0;
    for (const char *const *object = run->inputs; *object; object++) {
        const char *const *used = smith_run_lines(run, (const char *[]){"nm", "-uj", *object, nullptr});
        if (used == nullptr) return 1;
        for (const char *const *symbol = used; *symbol; symbol++) {
            const char *name = **symbol == '_' ? *symbol + 1 : *symbol;
            if (in_list(ours, *symbol) || in_list(run->args, name)) continue;
            fprintf(stderr, "%s uses %s from the C library, which is not allowed\n", *object, name);
            failed = 1;
        }
    }
    return failed; // 0 is success; smith creates any declared output the function left missing
}

void build(SmithBuild *b) {
    const char *const sources[] = {"base", "game", nullptr};
    const char *const *objects = (const char *[]){nullptr};
    for (const char *const *name = sources; *name; name++) {
        SmithStep cc = {
            .command = (const char *[]){"clang", "-O0", "-c", "$in", "-o", "$out", nullptr},
            .inputs = (const char *[]){smith_fmt(b, "%s.c", *name), nullptr},
            .outputs = (const char *[]){smith_fmt(b, "build/%s.o", *name), nullptr},
        };
        smith_step(b, &cc);
        objects = smith_cat(b, objects, cc.outputs, nullptr);
    }
    SmithStep check = {
        .run = check_libc,
        .args = (const char *[]){"memcpy", "memmove", "memset", "__memcpy_chk", "bzero", "sqrtf", "fabsf",
                                 "__stack_chk_guard", "__stack_chk_fail", nullptr},
        .inputs = objects,
        .outputs = (const char *[]){"build/check-libc.stamp", nullptr},
        .description = "check-libc",
    };
    smith_step(b, &check);
    smith_alias(b, "check-libc", check.outputs);
    smith_default(b, check.outputs);
}
```
`in_list` is a local helper: membership is a loop, so smith has no `smith_in`.

How ninja runs the function. The build program (build.c compiled and linked with smith's runtime) is also the thing that
runs `.run` steps. For each `.run` step, in order of creation, smith writes a ninja edge whose command is the build
program itself, a step number, and the lists:
```ninja
program = .smith/program
rule run
  command = $program --run $index $argsline --in $in --out $out
  description = $description
build build/base.o: cc base.c
build build/game.o: cc game.c
build task/check-libc: run build/base.o build/game.o | $program
  index = 0
  argsline = --arg memcpy --arg memmove --arg memset --arg __memcpy_chk --arg bzero --arg sqrtf --arg fabsf --arg __stack_chk_guard --arg __stack_chk_fail
  description = check-libc
build check-libc: phony task/check-libc
```
(In the real design the output is `build/check-libc.stamp`; `task/check-libc` is what was run. The block was updated to
the v1 path of the build program, `.smith/program`, and not rerun.) With `--run N` the
program runs `build()` again in replay mode, in which `smith_step` records and emits nothing, takes the N-th step that
has a `.run`, builds a `SmithRun` from the `--arg`, `--in`, `--out` lists, calls the function, and exits with its return
value. Replay (rather than a table of function pointers) is needed because the steps are created while `build()` runs,
and function pointers do not survive between processes. The replay gets the same `-D` arguments, so it makes the same
steps. `| $program` makes the program an implicit input: the function's code lives in the program, so a changed
`build.c` must rerun the step, and ninja's command text would not show that.

**Verified:** make: the shell version passes on a good tree (exit 0), fails with `build/game.o uses puts from the C library, which is not allowed` and exit 2 when game.c calls `puts`, and passes again once restored. The first run failed on `__memcpy_chk`, which Apple clang emits for `memcpy`: the allowed list needed it, as in the real Makefile. ninja: the same script as one `command`: good tree passes and creates the stamp; a second run did nothing; adding `puts` to game.c recompiled game.o, printed the same error and `FAILED: [code=1]`, and left no stamp; restoring it passed. For the smith form, `build.c` cannot run, so I wrote a stand-in (the `check_libc` logic above, using `popen` for `smith_run_lines`, plus the `--run N --arg A --in F... --out F...` parsing) and ran the ninja file above against it. Good tree: 3 edges, exit 0, then "no work to do". With `puts`: the error line names `build/game.o` and `puts`, exit 1, ninja printed the full command. Restored: passes. The stand-in wrote the stamp itself; in the real design smith does. Not verified: the replay step, which needs the real runtime. Zig 0.17.0: the 0.15 custom `Step` cannot be made to work. The first errors were `for (.{ ... }, 0..)` (the tuple index must be comptime) and `no field named 'id' in struct 'Build.Step.StepOptions'`: steps are a fixed set of kinds in the build runner (`Step.Tag`), with no custom `makeFn`. The version above, using `addObject` for `base.c` and `game.c`: the good tree passed (`nm -uj` on Zig's `base.o` shows only `___memcpy_chk`, which is on the list); a second run was `cached`; with `game.c` calling `puts` the step failed with exit 1 and `.../game.o uses puts from the C library, which is not allowed`; restoring `game.c` passed again (the `game` compile reran, the check reran). No file is produced; the result is the exit status.
**How each handles it:** make: a shell pipeline (`$$` everywhere), nothing structured. ninja: the same pipeline inside one `command`, with the `$` line continuations and the `$$` escape. Zig: with a workaround. A check written in Zig as a custom `Step` is not possible in 0.17 (no custom steps); the check runs as a shell script in a Run step with `expectExitCode(0)`, cached by the object files (a separate Zig or C program built by the same `build.zig` and run with `addRunArtifact` would be the structured alternative; not tried). smith: natively with `.run`, in C, using the same language as the project.
**What smith's API needs:** all of it is "later"; none of it is in v1 or v1.5.
- `.run` and `.args` in `SmithStep`. `.command` must be empty when `.run` is set; setting both is an error.
- `SmithRun` with `b`, `inputs`, `outputs` and `args` (lists end in `nullptr`). `run->b` lets `smith_cat` and
  `smith_fmt` work in the function with no second set of helpers; `b->root` is the working directory the function runs
  in.
- `smith_run_lines(SmithRun *run, const char *const *argv)` runs a program without a shell (`posix_spawn`), returns its
  standard output split into lines, and returns `nullptr` after printing a message if the program failed. Without it a
  function step has to write its own `popen`.
- The function's return value is the step's exit status; nonzero fails the edge. After a zero return smith creates any
  declared output the function did not write (a stamp needs no code). The function prints its own errors to standard
  error.
- The program is an implicit input of every `.run` edge (shown above), and the step number stays stable across runs of
  the same `build.c`.
- Dropped from the proposals: `smith_in` (membership is a loop, see `in_list`) and `smith_run_status` (an exit code
  alone; not needed here).
- Alternative to replay: pass the function's address as an offset from `main`. It is stable inside one binary but not
  portable (arm64e pointer signing), so replay is the choice.

### platform: a source file that differs by platform (`platform_macos.m` on macOS, `platform_linux.c` on Linux) and a framework flag only on macOS

**make** (GNU Make)
```make
CC := clang
OS := $(shell uname -s)

ifeq ($(OS),Darwin)
PLATFORM_SOURCE := platform_macos.m
LDFLAGS := -framework Foundation
else
PLATFORM_SOURCE := platform_linux.c
LDFLAGS :=
endif

build/app: main.c $(PLATFORM_SOURCE)
	@mkdir -p build
	$(CC) $^ $(LDFLAGS) -o $@
```

**ninja**
```ninja
# generated on macOS (b->os is "Darwin")
rule cc
  command = clang -c $in -o $out
rule link
  command = clang $in $flags -o $out
build build/main.o: cc main.c
build build/platform_macos.o: cc platform_macos.m
build build/app: link build/main.o build/platform_macos.o
  flags = -framework Foundation

# generated on Linux (b->os is "Linux"): platform_linux.c, and no flags line
```

**build.zig** (Zig 0.17.0, run)
```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const macos = target.result.os.tag == .macos;

    const module = b.createModule(.{ .target = target, .link_libc = true });
    module.addCSourceFile(.{ .file = b.path("main.c") });
    module.addCSourceFile(.{ .file = b.path(if (macos) "platform_macos.m" else "platform_linux.c") });
    if (macos) module.linkFramework("Foundation", .{});

    b.installArtifact(b.addExecutable(.{ .name = "app", .root_module = module }));
}
```

**smith build.c** (v1)
```c
#include <smith/build.h>
#include <string.h>

void build(SmithBuild *b) {
    bool macos = strcmp(b->os, "Darwin") == 0;
    const char *platform = macos ? "platform_macos" : "platform_linux";
    const char *extension = macos ? ".m" : ".c";

    const char *const names[] = {"main", platform, nullptr};
    const char *const *objects = (const char *[]){nullptr};
    for (const char *const *name = names; *name; name++) {
        // clang picks Objective-C from the .m extension; no -x is needed.
        SmithStep cc = {
            .command = (const char *[]){"clang", "-c", "$in", "-o", "$out", nullptr},
            .inputs = (const char *[]){smith_fmt(b, "%s%s", *name, *name == platform ? extension : ".c"), nullptr},
            .outputs = (const char *[]){smith_fmt(b, "build/%s.o", *name), nullptr},
        };
        smith_step(b, &cc);
        objects = smith_cat(b, objects, cc.outputs, nullptr);
    }
    SmithStep app = {
        .command = smith_cat(b, (const char *[]){"clang", "$in", nullptr},
                             smith_when(b, macos, (const char *[]){"-framework", "Foundation", nullptr}),
                             (const char *[]){"-o", "$out", nullptr}, nullptr),
        .inputs = objects,
        .outputs = (const char *[]){"build/app", nullptr},
    };
    smith_step(b, &app);
    smith_default(b, app.outputs);
}
```

**Verified:** this host is macOS, so the macOS path ran for real and the Linux one only as far as it can on a Mac. make: `make` printed `clang main.c platform_macos.m -framework Foundation -o build/app` and the program printed `macos 1`; `make OS=Linux` printed `clang main.c platform_linux.c  -o build/app` (no framework, no .m) and the program printed `linux`. ninja: `mac.ninja` built `main.o`, `platform_macos.o` (Objective-C picked from the extension) and linked with `-framework Foundation`, printing `macos 1`; `linux.ninja` built the C-only version, printing `linux`. I did not run on Linux, and I did not run a cross-compile. Zig 0.17.0 (the snippet ran unchanged): `zig build` built the native macOS target with `platform_macos.m` and `-framework Foundation`, and `zig-out/bin/app` printed `macos 1`. `zig build -Dtarget=x86_64-linux-gnu` selected `platform_linux.c` and produced an x86-64 ELF executable (not run; it contains the string `linux`). `zig build -Dtarget=aarch64-macos` failed with `unable to find system framework "Foundation". searched paths: none`: an explicit macOS target does not find the SDK that the native target finds.
**How each handles it:** make: natively with `uname` and `ifeq`. ninja: not at all; the generator picks the files and writes a different graph per platform (smith does exactly this). Zig: natively (`target.result.os.tag`, `linkFramework`, cross-compile aware for Linux; an explicit macOS target does not locate the SDK frameworks). smith: natively, plain C on `b->os`.
**What smith's API needs:** nothing new (v1), given that `b->os` and `b->arch` are `uname -s` and `uname -m` exactly
("Darwin", "Linux"; "arm64", "x86_64"), so they match what a Makefile sees. The `strcmp` is the only awkward line. When
the compile command carries `-target`, `b->os` as the host's OS is wrong for this case. Zig has a separate target and
host; make's `uname` has the same fault. If cross-compiling matters, `SmithBuild` needs a target OS next to `os` (an
open decision in [build-c.md](build-c.md)); it is not in v1.

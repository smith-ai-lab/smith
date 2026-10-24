---
type: architecture
title: The .smith language
description: A .smith file is C with modules, lowered to C as clang reads it by a token hook, with sized types, the rules, one command, and what the language does not do yet.
tags: [smith-lang, lowering]
---
# The .smith language

**`.smith` files are C with modules, lowered to C as clang reads them** ([the language](smith-language.md), issue
#11). The one change to clang is a token hook on its `Preprocessor` (`patches/clang-token-hook.patch`, about 100
lines, applied by `scripts/llvm.sh`), which offers a `TokenHook` the identifiers it watches, after macro expansion and
before the parser; plain C in a `.smith` file costs the compiler under 1%. The language is smith's own C
(`runtime/lang*.c`, which knows nothing of clang), and the clang side is `shim/hook.cpp`: the hook, and the parser's
name lookup as the lowering's questions. A second patch (`patches/clang-debug-name.patch`, about 60 lines, a process-wide
hook in `CGDebugInfo`) lets the debugger show a module's names as written: `geometry::box::area` for a function or variable
(its `DW_AT_name`), `Box` in namespaces `geometry::box` for a type; plain C debug info is byte-identical
(see [the debugger](debugger.md)). Modules use `::` (`module a::b;`, `x::name`, `c::printf`), never dots. The
`.smith` extension turns it on (`smith cc x.smith` is `-x c -std=gnu2y x.smith`: `.smith` is smith's own language and
compiles as C2y, the standard `smith new` uses, so `auto`, `::` and `_Countof` work, unless the caller names a
`-std=`, which wins); plain C in a `.smith` file means what it means in a `.c` file (`tests/smith-lang-c.sh`).
`smith cc main.smith foo.smith` just works: an `import foo` that finds no `foo.h` on the include path reads the
module's source (among the command's `.smith` files, else at the path its name implies under the importing module's
root) and makes its interface in memory, writing nothing; `smith build` still writes the headers and they win. The
sized types (`u32`, `f64`, ...) are typedefs, made when a file first uses one it does not declare itself, so a
diagnostic reads `'i32' (aka 'int')`; a generated C header still says the standard C types. **A module's generated header includes exactly what its exported declarations need**: `<stddef.h>`, `<stdint.h>`, an imported module an export writes `x::Name` of, and a C header (`import <h>;`) an export writes `c::name` of (with several imports smith asks the compiler which declares it; a stage0 keeps them all); an `#include`, and an import only a body uses, never reach it, so a module whose exports use a header's types `import`s it (`tests/headers.sh`). The lowering reports a C
keyword used as a name (`i32 double(i32 x)`) itself, at the name. A smith whose clang lacks the hook, a stage0 included, refuses a `.smith`
input; it can still write a module's header (`smith cc --smith-header`). The compiler's helpers are under `smith cc`
(`--smith-header`, `--smith-lower`, `--smith-which`), since smith's top level is only verbs. `smith build` builds the
modules itself: a header step per module, and every `.smith` compile, and every C compile that names a header of `.smith/include` as an input or puts that folder on its include path, waits for all of them (knowledge/build-file.md).

A `.smith` file is C with modules. Renaming `x.c` to `x.smith` changes nothing: the file means what it meant.
**The C rule:** valid C in a `.smith` file without a `module` line means exactly what it means in C, at the same `-std` as a `.c` file: the latest C by default; an older C is an explicit `-std=`. A new feature uses
syntax that is invalid C (`::`, `import x;`, `auto` and `constexpr type` parameters, `if constexpr`); a `module` line may
reject valid C (it defines exports and private names) but never silently changes what a construct means. A module-only
rejection gets an error case in the corpus (`tests/smith-lang/cases/error-*`), and `tests/smith-lang-c.sh` pins the rule:
smith's own C sources, and the programs of `tests/smith-lang-c/` (array parameters, `sizeof` of one, an `int a[*]`
prototype, the language's words as names, ordinary constructs), give the same code and output as `.c` and as `.smith`. Known exceptions, both issues: an enum
constant or function named like a sized type fails loudly (#91), and the default dialect is C2y, not the `.c` default (#92).
**`defer`** (issue #210, skill `smith-lang-defer`): in a file with a `module` line `defer` is the keyword of C2y's defer TS and lowers to clang's `_Defer`; the meaning is exactly the TS's (last deferred first, on the end of the block, `return`, `break`, `continue`, `goto` out), no extension, and what the TS forbids is an error. `smith_lang_command` (runtime/lang_command.c) adds `-fdefer-ts` to every command with a `.smith` input (it works under `-std=gnu2y`), a plain `.c` command is left as it is. `defer` is a watched name (`smith_lang_watch_basics`): the hook (`deferToKeyword`, shim/hook.cpp) writes `_Defer` for it in a module, except after `.` or `->`, and reports an error where a type stands before it (a declarator); without a module line it passes as a name (`tests/smith-lang-c/defer_name.c`). `smith fmt` needs no patch (clang-format reads `defer stmt;` as a statement); the affine analysis sees `_Defer` (`tests/affine/affine_defer.smith`). Corpus: `defer-run`, `error-defer-name`.

**Functions at compile time** (issue #97, LLVM `23.1.3-r6`): `patches/clang-constexpr-c.patch` lets clang's constant evaluator run the body of any C function defined in the file, in a `.smith` file (the hook sets `LangOptions::SmithConstexprC`; a `.c` file never has it), but only where C requires a constant and the program was invalid without it (`constexpr` initializers, `static_assert`, case labels, enum values, bit-field widths, static initializers): the gate is `Info.InConstantContext && EvalMode != ConstantFold`, so quiet folding (`__builtin_constant_p(f(1))`, a GNU-folded array size, an automatic initializer) is unchanged and `int a[f()]` in a function stays a variable-length array. The patch only turns an error into a value, so the C rule holds; `tests/smith-lang-c.sh` pins it (`folding.c` gives the same code as `.c`, as `.smith` and before the patch; `constexpr i32 x = fib(10);` stays an error in a `.c` file, `sizeof(long)` is 4 for a Windows target). The step budget and call depth are clang's (1048576, 512); undefined behaviour is an error with clang's notes. The option is `Benign`, so it is on under `-fmodules` too (r6; with r5 the hook left it off). A failed contract or check in such a call is a compile error in the run-time report's form ([failure-reports-and-tests](failure-reports-and-tests.md)), and a `constexpr` pointer may point into a string literal or a `constexpr` object (the gate in `SemaInit.cpp`; a `.c` file still rejects it). Corpus: `constexpr-c`, `constexpr-modules`, `constexpr-pointers`, `error-constexpr-c`, `error-constexpr-c-asm`, `error-constexpr-contract`, `error-constexpr-contract-deep`, `error-constexpr-pointers`. A function of another module runs the same way: the module's own compile writes its lowered functions to a `.body` file and the importer that mentions one where C requires a constant defines it ([body-files](body-files.md), issue #107; corpus `constexpr-import`, `constexpr-import-header`, `constexpr-import-result-type`, `error-constexpr-import-state`). A call the evaluator ran is no GNU folding (r7, `patches/clang-constexpr-fold.patch`, #166): `static_assert(f(x) == 5)`, enum values and case labels build under `-Wpedantic -Werror`. Skill `smith-lang-constexpr`.
**Run-time arrays** (issues #82 and #105, runtime/lang_slice.c, skill `smith-lang-arrays`): `T xs[:]` is a slice, a pointer and the number of elements after it, the struct `struct smith_array_<element>_n { T *data; size_t count[1]; }` written where it is declared (C23 accepts the same definition again; the tag is spelled by the tuples' reader, and no name of the module's). `[:]` in a declarator is invalid C, so the C rule holds, and a file whose text has a `[ ]` with a `:` that is no `::` and no `? :` is offered every token (`host->maybe_slice`, which also switches off the hook's fast path: the lowering reads each token) and an import's header says `// smith-slices`. A declaration is found at its first token (after `;`, `{`, `}`, `(`, `,` or `:`), never at the `;` before it: tokens the lowering reads ahead are processed in the same batch, before the parser has the declarations of the statements before, so what asks the compiler a type (`expr_type`) must start at a token the parser has just consumed. The leading `static`, `typedef`, `export` and attributes stay outside the struct. `xs[i]`, `xs[a:b]`, `xs[:b]`, `xs[a:]`, `p[a:b]` (a pointer or a fixed array: unchecked extent, both bounds) and a call whose arguments need `T[N]` to `T[:]` or `xs` to `xs.data` (a `c::` function) are found by reading the whole postfix chain at its first token from the queue and typing each prefix with `expr_type`, since a subscript is read after the expression it subscripts has gone to the parser; the chain is written again as `(*__extension__ ({ auto s = (xs); size_t i = (i); check; &s.data[i]; }))` and the like, the check being `smith_failure(4)` (an assert, so the report is `contract::assert`'s, with the line of the index) when `target::assertions`, so the program links the trace library and fast builds check nothing. `=` and `return` convert a fixed array or a string (count without the zero) the same way. A slice a typedef or a global defined at file scope is named `struct tag` alone afterwards, with the definition in the token's `header_text` (a second definition inside the head of an export, after one at file scope, lost the `{` that follows the head). Errors: a step, a negative constant index, a pointer slice with one bound, a pointer passed as a slice, a slice across rows, a fixed array of more dimensions sliced, an array of another shape than the slice. **More dimensions** (#105): `f32 xs[:][2]` and any mix of `[:]` and numbers (at most 8, the first a `[:]`) are one block, row-major, `struct smith_array_f32_n_2 { float *data; size_t count[k]; }` with k the `[:]`, the fixed sizes only in the tag (`_n`, `_2`; an element whose name ends like a dimension gets `_e` between, so the tag reads back from its end: `smith_lang_slice_dims_parse`); a run of indexes `xs[i][j]` is one statement expression (`smith_lang_slice_rows`: Horner offset, one check per dimension), the element when it names every dimension, else a row (a `[:]` of the dimensions left, its struct defined in the expression with `__typeof__(*s.data)`); `xs[a:b]` of rows is a view of the same shape (`smith_lang_slice_view_rows`); a fixed `f32[4][2]` converts by `&a[0][0]` and the array's dimensions (`SmithExprType.array_rank`, `array_dims`, `array_element`, which the shim fills); `countof(xs[i])` rewrites its operand as a slice when the base has more dimensions than indexes. No strides. The raw lowering (`--smith-lower`, headers) rewrites declarations and leaves expressions, which need the compiler. Corpus: `slice-lowering`, `slice-run`, `error-slice-*`; run time: `tests/slice_test.smith`; C rule: `tests/smith-lang-c/slices.c`. A generic can return a slice, `typeof(T[:]) push_slice(Arena *arena, constexpr type T, usize count)`: the struct's tag is `smith_array_T_n` in the recorded head (flag `g`, `smith_lang_gen_flag_slice`), which an instance spells for its argument as `T xs[:]` would (`u8` back from `__UINT8_TYPE__`); a body is recorded before T is known, so it names the struct itself. The head of `if (`, `while (`, `for (` and `switch (` starts no chain (reading its group looks at the body's first token before the parser has the header's declarations). A program that writes a slice links the trace library with no line in its build file: `smith_app` finds `[:` or `:]` in a `.smith` source. The array expression of an index or a slice is bound once (`auto smith_s = (...)`), so a call in it runs once (`slice-run`, `tests/slice_test.smith`). Not yet: `format` and `test::check_equal` over slices, `arena::push_slice` of more sizes (#105), the prover, the debugger and the editor (#106).
**Qualifiers on a slice** (issue #126, runtime/lang_slice.c, skill `smith-lang-slice-qualifiers`): C's rules for a pointer, carried over. The element's qualifiers are part of the element type, so the tag spells them (`smith_array_const_u8_n`, `u8 const xs[:]` and `const u8 xs[:]` read to one tag, because the tuples' reader orders qualifiers) and the struct holds a `const u8 *`. The slice's own go inside the bracket, after C99's `int a[const 10]`: `[const :]` puts `const` before the declaration's struct, `[restrict :]` makes the data pointer `__restrict` in a struct of its own (`smith_array_restrict_u8_n`, `SmithLangSliceDims.restrict_data`, the tag read back by `smith_lang_slice_dims_parse`; a cut or a row of it is the plain struct, `SmithLangSliceType.plain`), `static` and `volatile` are errors, and a qualifier in a later bracket is an error (only the outermost bracket may carry qualifiers, as in C; a row is a view with no pointer to qualify). A slice where another tag is wanted (`smith_lang_slice_convert`, so a call, `=`, an initializer and `return`) converts by C's pointer rules (`smith_lang_slice_relation`): adding `const` or changing `restrict` is a statement expression that copies the pointer and counts (`smith_lang_slice_requalify`), taking `const` off is the error "discards qualifiers" (the lowering's own, so it does not depend on `-Werror`). The type of a cut `p[a:b]` passed where a slice is wanted is read before it is lowered (`smith_lang_slice_cut_type`), and a struct only a parameter list defined is written inside the conversion (`smith_lang_slice_definition_text`, since a prototype's scope is gone at the call). The slice's own qualifiers are no part of the type a value holds (`smith_lang_ty_from_tag` skips `restrict_`); a slice by value is two integers, so the IR has no `noalias`, and `restrict` is in the debug info (`DW_TAG_restrict_type` on `data`). Corpus: `slice-qualifiers-run`, `slice-qualifiers-lowering`, `generic-slice-const`, `error-slice-const-*`, `error-slice-qualifier-bracket`; tree-sitter: `repeat(type_qualifier)` in the four array declarators.
**An enum with payloads** (issue #198, runtime/lang_enum.c, PR 1 of 3): `typedef enum { Circle circle; Rect rect; empty; } Shape;` has its variants written like union members (a type and a name, an anonymous struct, or a name alone) and lowers, at the `enum` token (`smith_lang_enum_begin`: braces with a `;` at their own depth, which no C enum has), to `struct Shape { enum Shape_tag { Shape_tag_circle, ... } tag; union { Circle circle; ... }; } __attribute__((annotate(\"smith::enum\")))`: the tags are module names like any (`main_Shape_tag_empty`), the annotation is how a plugin tells the struct from other structs, and the header gives C callers the struct (unchecked). A variant is written with a leading dot and looked up in the type the context gives, which the lowering asks the compiler for (`smith_lang_enum_type`: a struct whose name has a tag enum `<name>_tag`): a declaration's or assignment's left side (`smith_lang_enum_equals`; a declaration writes C's `{.tag = C}` initializer, an assignment a compound literal), `return` (`smith_lang_enum_return`), a parameter (`smith_lang_enum_argument`, from the callee's type), the other side of `==` and `!=` (`smith_lang_enum_compare`, `...compare_first`: `s == .v` is `(s).tag == C`) and the value switched (`smith_lang_enum_switch`: `switch ((s).tag)` inside `_Pragma` push and pop that make -Wswitch-enum an error for the region, `default:` or not; `case .v:` is read in the innermost such switch, `smith_lang_enum_brace` counts the braces that close it). A dot with no type is an error naming the fix, and so is a variant the type does not have and a payload variant used alone. A file that has one is in slice mode (`slices_seen`, `// smith-enums` in its header), since the contexts are the chain code's. Narrowing (reading `s.circle` only where `s == .circle` holds) is not done yet: the plugin will do it from the annotation and the tag. `smith fmt` writes the declaration and `case .v:` untidily until clang-format's patch learns them. Corpus: `enum-payload-run`, `enum-payload-module`, `enum-payload-lowering`, `enum-c-rule`, `error-enum-payload`; tree-sitter: `payload_variant_list`, `variant_expression`, `designated_list`.

**`str`** (issue #85, runtime/lang_slice.c `smith_lang_slice_str`, skill `smith-lang-sized-types`): in a module the word `str` is the type `const char[:]`: at the token the lowering writes `typeof(const char[:])` into the queue, so everything that holds for that spelling holds (a string converts with its count, `str *p` and the initializer lists of #148 and #173; the header shows the slice struct, no typedef). Unlike `u8` it is a rewrite in the slice path, not a typedef the parser knows, so the hook offers every token of a file that has the word (`hasStr`, `smith_lang_text_has_str`) and the rewrite stops at a plain file (the C rule), at a name the module declares itself (its `typedef const char str[:];`), after `.`, `->`, `::`, `struct`, and errors where `str` follows a type or a `*` (a declarator: it is a type name here). A generic's recorded head and body are not rewritten yet, nor an array of `str` with braces (an error).

**A pointer to a slice** (issue #148, runtime/lang_slice.c, skill `smith-lang-slice-pointers`): C's pointer to an array, `const char (*text)[:]`, as `int (*p)[10]`. `smith_lang_slice_pointer_begin` reads the declarator at a declaration's first token and writes the slice's struct, then the pointer's stars, qualifiers and name (`struct tag *text`), so `typeof(const char[:]) *text` and a typedef'd `Text *` are the same type and `*text`, `(*text)[i]` and `countof(*text)` are the ordinary slice operations. At a call a parameter that is a pointer to a slice is marked by a star in `smith_lang_slice_params`, and `smith_lang_slice_convert_pointer` applies C23's rule for pointers to arrays (N2607) to the argument: the same struct passes, adding `const` to the element casts the pointer (the structs differ only there, and `smith_lang_slice_relation` says which way), taking it off is the error "discards qualifiers". An initializer does the same (`armed_tag` starts with `*`). A declaration whose type is a slice by a typedef or `typeof` (`Text s = "abc";`) is read at its `=` by `smith_lang_slice_declared`: the name is not declared yet, so the compiler is asked the type of `( Text ) { }` (and `SmithLangSliceState.typeof_tag` holds the slice of an inline `typeof(...)`). Corpus: `slice-pointer-run`, `slice-pointer-lowering`, `error-slice-pointer-discards`. A brace initializer of a struct with slice members (issue #173) is walked by `smith_lang_slice_init_walk` (frames of open structs, `type_field` for the member each value is for, `.name =` and positional, nested braces; `smith_lang_slice_init_equals` at the `=` of a declaration, `smith_lang_slice_init_compound` at the `{` of `(T){...}`): a slice member's value goes through `smith_lang_slice_convert`, braces for a slice, a deeper designator and what the walk cannot read (an array of structs, an anonymous member, over 200 tokens; `SmithFieldInfo.has_slice` from the shim says the struct holds a slice) are errors. Corpus: `slice-initializer-run`, `error-slice-initializer`, `error-slice-initializer-unread`.
**Tuples** (issue #96, ticket 1 of #95): `struct { i32, f32 }` is a struct whose fields have no names, a syntax error in C, so it is smith's. Its identity is its shape: the same field types in order are one type in every file and module (`runtime/lang_tuple.c`, skill `smith-lang-tuples`). The lowering reads the `struct` token's lookahead: a `{` and, before its matching `}`, no `;` is a tuple (a normal struct has one), and the tokens up to the `}` become `struct smith_tuple_<fields> { A _0; B _1; }`, written where the tuple is (C23 accepts the same definition again in any scope), so every state of the lowering sees an ordinary struct. The tag is `smith_tuple_` and the fields' spellings joined by `__`, built by one non-recursive pass with a frame stack (a nested tuple is `tuple_..._end`, a pointer `_ptr`, a long spelling is cut and hashed) and the same in every pass (the module's compile, its header, the raw lowering, which has no parser): a field type is named by what is written with a module's names resolved (`Meters` in its module and `a::Meters` elsewhere are `geo_a_Meters`), and the sized, stdint and basic C types are unified (`i32` = `int32_t` = `int`; the table assumes the 64-bit platforms smith runs on), but a user typedef is **not** looked through, since the header pass has no parser to do it: `struct { Meters, f32 }` and `struct { f64, f32 }` are different types though `Meters` is `f64`. An array, a function pointer or a `typeof` field is an error that asks for a typedef. The tag is no name of the module's (`smith_lang_lower_tag` keeps it), a head holds a tuple's braces like parentheses (`SmithToken::tuple_brace`, so `export struct { i32, f32 } make(...) {` is one head and not an aggregate with a declarator after it), and the tokens the lowering adds (tag, `_0`, `;`, the `(struct T)` of `return {a, b}`) are `synthetic`: with them counted as source tokens clangd hung.
**`fields(T)` is an array of `Field`** (issue #100, ticket 5 of #95; [smith-generics](smith-generics.md), "Reflection"): `Field` is a struct with a `name` (a `char[:]`, a pointer into a literal, so the evaluator's `constexpr` pointers of #103 are needed) and a `type` (a handle), `fields(T)[i]` an ordinary compile-time value that can be stored in a `constexpr Field`, passed to a function, returned and compared, and `x.[f]`, `x->[f]`, `typeof(T.[f])` take any `Field`. It is invalid C (`fields`, `.[`), and `Field` is a name the lowering defines in the files that use it, so the C rule holds. Skill `smith-lang-reflection`; corpus `generic-fields`, `generic-fields-values`, `generic-fields-import`, `generic-fields-error`.
`t[0]` is `t._0` and `countof(t)` the number of fields: at a `[` followed by a number or a `constexpr` name, or at `countof`, the lowering takes the operand from the host's record of the last tokens (`host->previous`; the kept tokens of a generic call's arguments; the tokens of this batch) and asks the compiler its type (`expr_type`); a tuple's tag starts `smith_tuple_`, the field count is `type_info`'s, and a non-constant or out-of-range index is an error that names `struct { i32, f32 }` and its count (the index tokens are still taken and `._0` written, so the compiler says nothing more). `return {a, b};` in a function whose return type the compiler gives (`host->return_type`) as a tuple is a compound literal of it. The hook offers every token of a file whose text has `struct {` with no `;` before its `}` (`host->maybe_tuple`), and of a file that imports a module whose header says `// smith-tuples` (the meta line a module that uses tuples writes), since `struct` is a keyword and `t[0]` is found among all tokens; other files are as fast as before. A generated header has each definition hoisted out of the signatures, before the declaration that uses it, once, behind `#ifndef SMITH_TUPLE_<fields>` (innermost first), so C11 and C++ include it; the in-memory interface an importer reads (`aligned`, the module's lines) keeps them inline. clang-tidy's naming rules skip `smith_tuple_*` and its fields (`shim/tidy.cpp`), and the tree-sitter grammar has `tuple_field_list` and `return {...};`. Known gaps: a tuple type written in a generic function is an error that asks for a typedef, and `t[0]` and `countof(t)` are not rewritten in a generic's body (`t._0` works): replay writes the recorded tokens without the lowering's token path (ticket 6 of #95 turns `auto... rest` into a tuple). Corpus: `tuple-basic`, `tuple-spelling`, `tuple-modules`, `tuple-import-index`, `tuple-c`, `error-tuple-index`, `error-tuple-empty`, `error-tuple-field`, `error-tuple-typedef-distinct`; run time: `tests/tuple_test.smith`.
A file opts
into more, step by step: imports and sized type names work in any `.smith` file; a `module` line makes it a module, with
exports and private names, and a function with `constexpr type T` or `auto` parameters is generic. The design and the issues it answers are in
[issue #11](https://github.com/adam-r-kowalski/smith/issues/11); this page is what is built.

```c
// src/game/replay.smith
module game::replay;                // must match the path: .../game/replay.smith
import lib::text as tx;             // a smith module, bound to `tx`
import <string.h>;                  // a C header; its names are reached as c::name

export typedef struct { u64 frames; const char name[:]; } Replay;
export constexpr u32 version = 3;
export u32 counter = 7;
export Replay open(const char *path) { … c::strlen(path) … }
export u64 checksum(const u8 *data, usize size);   // no body: defined in C or C++
u32 helper(void) { … }                              // private
```

- **Imports** work in any `.smith` file and come before any declaration (`#include` stays allowed anywhere).
  - `import a::b::c;` binds `c`; `import a::b::c as x;` binds `x`. Members are reached with `::`: `x::open`,
    `x::Replay`, `struct x::Pair`. `x::` is always a module and `x.` always a value, so a variable may share its
    module's name (`io::Io io = io::make();`): `io` is a binding only where `::` follows it, and never inside an
    attribute (`[[gnu::unused]]`). This needs C23, which lexes `::` as one token.
  - `import <h>;` and `import "h";` include a header and bind `c`: `c::sinf(x)`, `c::EOF`, `c::WIFEXITED(s)`,
    `struct c::stat`. A quoted header is looked for beside the importing file first. An import always puts the
    header's names under `c::`, in a module or not; `#include` keeps them bare, as in C.
  - `c::` names C only in a file that imports a header; `c` is otherwise any identifier, and `c.x` is a field.
- **Modules.** The first line, `module a::b;`, makes the file a module; its path must end in `a/b.smith`.
  - Every file-scope name in a module is prefixed with the module's name (`game_replay_open`). `export` makes a
    declaration public; everything else is `static` (functions and variables) and used nowhere else.
  - **A foreign C symbol** is declared with `extern` and no `export`: `extern char **environ;`, `extern u32 counter;`,
    `extern u32 triple(u32 x);`. It keeps its C name (no prefix, no `__asm__` label, not in the header), so the module
    links to the symbol C defines and uses it bare, as C does. For a symbol a header declares, `import <h>;` and
    `c::name` do the same (`c::optind`, `c::stdout`); `extern` is for one no header declares (macOS's `<unistd.h>` has no
    `environ`). `export extern T name;` is the opposite: the module's own symbol, defined in C by the prefixed name.
  - `export` applies to functions, typedefs, struct, union and enum tags, enum constants, `constexpr` constants and
    globals. An export has the symbol `game::replay::open` (an `__asm__` label), so no C name can collide with it.
    `main` stays `main`.
    An exported variable is declared before its definition in the module's own compile (`extern T name __asm__(…);`,
    what its header says), as a C file that includes its own header has it, so a `-Werror` project with
    `-Wmissing-variable-declarations` compiles a module with no pragma.
  - **`[[smith::c_name("Name")]]` before `export` chooses the C name** of the export, for a library that keeps its C
    API when it becomes a module (`[[smith::c_name("SmithArena")]] export typedef struct {…} Arena;`,
    `[[smith::c_name("smith_text_parse_f64")]] export bool parse_f64(…)`).
    - What C sees is exactly that name: the generated header declares `SmithArena`, and the symbol of a function or
      variable is that name too (no `__asm__` label: `_smith_text_parse_f64` on macOS), so C and C++ call it, define
      an export that has no body in the module, and link against it as against any C function. Other exports keep the
      prefixed name and the `::` symbol.
    - A `.smith` importer still writes the module's name (`arena::Arena`, `text::parse_f64`) and gets the C name: the
      header's `// smith-names:` line lists `Arena=SmithArena`, which is how the importer's lowering, its diagnostics
      and its editor know the pair. In the module itself the name is the C name in the generated code.
    - It goes on the export's first name (the tag, in `export typedef struct S {…} S;`, and the typedef of the same
      name with it); an exported enumeration's constants are `<CType>_<Value>` in C, the C name of the enum (this one's, or its tag's, `mix_modes_Level_High`) and the constant, which is C's own naming rule (`MixMode_Fast`), while `.smith` importers write `modes::Fast`; an anonymous enum typedef without it keeps the prefixed names. It is a string holding one C identifier.
    - Errors: a C name another export already has chosen; one that the module's prefix gives another name (a private
      one included); a name declared later that the prefix would make the same; `[[smith::c_name]]` not followed by
      `export`; two on one export; an export that declares no typedef, tag, function or variable; a string that is no
      identifier (or a keyword), or an argument that is no string. It is the only attribute in its `[[ ]]`.
  - **A parameter keeps its name** when the module exports one by that name (`export void step(Step *step)`,
    `export i32 area(i32 width)` beside an export `width`): the parameter is not the export, so the header and the
    body say `step` and `width`, as C scopes them (inside the body the export of that name is out of reach, as in C). A
    local declared in a body is not tracked this way: it is renamed like the export, consistently.
  - A C name used with no `c::` is an error in every `.smith` file, a module or not: a function, a variable, a type,
    a tag or a macro that an `import`ed header declared. Names from `#include`d headers keep working bare, as in C.
    A file with no `module` line lowers nothing else: its names are not prefixed and nothing becomes `static`.
    Inside an attribute (`[[gnu::format(printf, 1, 2)]]`, `__attribute__((format(printf, 1, 2)))`) a word is the
    attribute's own, not a use of a C name: `printf` there needs no `c::`. The keywords of C23 that a header may define as
    a macro (`static_assert`, `alignas`, `alignof`, `bool`, `true`, `false`, `nullptr`, `thread_local`) are the
    language's, not C names.
- **Sized types**, in any `.smith` file: `u8 u16 u32 u64`, `i8 i16 i32 i64`, `usize` (`size_t`), `isize` (`ptrdiff_t`),
  `f16` (`_Float16`), `f32`, `f64`. They are typedefs of the standard types (`typedef __INT32_TYPE__ i32;`), so the same
  types (`%llu`, `_Generic` and a C header's `uint64_t` agree) and a diagnostic names them as written:
  `initializing 'i32' (aka 'int')`. The typedef is made when the file first uses the name and declares nothing of that
  name, so a file that declares its own `u32` (a variable, a file-scope function or an enum constant named `u8`) keeps it, and a C header's `typedef uint32_t
  u32;` is fine (it conflicts only where the types differ, and then clang says so). The typedefs are implicit: they are
  in no header. A module's generated C header says `uint32_t`, so a C file that includes it has no `i32`.
- **C is C2y** in a `.smith` file: `smith cc x.smith` adds `-std=gnu2y` (the standard `smith new` uses), so `auto x = f();`,
  `::` and `_Countof` work with no flag. A `-std=` of the caller's wins, and none is added beside a C++ input.
- **Errors**, each pointing at the `.smith` text: an exported declaration naming a private type; an import cycle
  (`a -> b -> a`); `export` in a file with no `module` line; a module name that
  does not match the path; a bare C name from an imported header; an import after a declaration; a module that is not found; the
  `module` line not first; an export that declares two names; a C keyword used as a name.
  - **A module that is not found** is one error, naming what was tried (``cannot find module `foo`: no `foo.h` in the
    include paths, no `foo.smith` among the input files, and nothing at the path the name implies (`src/foo.smith`)``),
    and the uses of its names (`foo::f`) report nothing more. The same for a cycle and for a module whose source has an
    error.
  - **A keyword where a name goes**: after a sized type name or a typedef of the module (and `*` or a qualifier), a C keyword that cannot continue
    the type is the name of something. ``foo.smith:3:12: error: `double` is a C keyword; it cannot name a function``,
    and likewise a variable, parameter, field or type. The name is then read as an identifier, so clang adds no cascade
    (a later use of the misnamed thing in an expression, `return int;`, is still clang's "expected expression"). A
    keyword after a typedef of the module (`Box double`) is reported the same way, as one error; after any other type
    (`Point double(...)` with a typedef from a header) it is clang's own message.
- **Not yet:** selective imports, re-exports, a module over several files.

```
smith cc main.smith foo.smith -o prog      # main.smith has `import foo;`; foo.smith is `module foo;`
smith cc -c main.smith                     # finds foo.smith by path, too
```

When `import foo;` finds no `foo.h` on the include path, smith looks for the module's source: first among the `.smith`
files on the command line (`SMITH_LANG_INPUTS`, which `smith cc` sets for the compiles it runs), then at the path the name
implies under the importing module's root (for `module main;` in `dir/main.smith`, `import foo` is `dir/foo.smith`, and
`a::b` is `dir/a/b.smith`; a file with no `module` line has its own folder for a root). The module's interface (what
`smith cc --smith-header` would write) is made in memory and read as the header, which the compile's depfile names by the
source; nothing is written. A header on the include path (as `smith build` writes them) is used instead, and is no
slower. Linking is as in C: name every source on the command line, or use a build.c. On a laptop under load, the compile
of a small `main.smith` took 21.5 ms with a header on the path and 22.0 ms reading the source (60 alternating runs).

A `.smith` file is lowered to C as the compiler reads it; there is no intermediate file. `smith cc --smith-lower x.smith`
prints the lowering (without macros or the parser's help, so `c::EOF` shows as `EOF`); `SMITH_LANG_DUMP=<file>` writes
what the compiler's own run lowered. The corpus (`tests/smith-lang/cases`) keeps both for every rule.

`--smith-lower` keeps the file's preprocessor directives as written, each on a line of its own at its place between the
tokens: `#include`, `#define`, `#undef`, `#pragma`, `#error`, `#warning`, `#line` and the conditionals (`#if`, `#ifdef`,
`#ifndef`, `#elif`, `#else`, `#endif`), so the compiler that builds the lowered C decides them. It evaluates none, so every
branch of a conditional is lowered, by the same plain path as the rest (a branch the build machine does not take needs no
answer from a parser: sized types are the compiler's built-in types and `c::NAME` is `NAME`; `tests/smith-lang/cases/lowered-directives`).
Left out: comments, and a chain that starts with `#if 0` (its text is not read; its `#else` stands alone and an `#elif B`
becomes `#if B`). A `#define` whose body has `::` or a sized type is an error at that line, since it would reach the
compiler as written ("make it a function"). The compiler's own run needs none of this (it has preprocessed them); a
`#pragma` such as `STDC FP_CONTRACT` reaches the hook as an annotation token and goes to the parser as it is.

`i32 main(io::Io *io)` in a `.smith` file (it needs `import smith::io;`, under any name) is the program's body, with the
io made for it: the lowering writes it as `static int32_t smith_user_main(SmithIo *io) { ... }` (the module's prefix is
never put on it) and, at the end of the file, `int main(int argc, char **argv)` that makes `SmithIo io =
smith_io_make(argc, argv)`, calls `smith_user_main(&io)`, runs `smith_io_close(&io)` and returns the status. It is found
by looking ahead from the `i32` that begins a file-scope declaration (`i32`, `main`, `(`, the binding, `::`, `Io`, `*`,
a name, `)`), in a file with a `module` line or none and in `--smith-lower`; the queued `main` is renamed, so the rest
of the lowering sees a private function. The C `main` has the location of the name you wrote (the debugger shows
it at your `main`'s line), the table of names says `smith_user_main` is written `main`, the debug info names it `main` (the debug-name hook of `shim/hook.cpp`, with a module line or none, `tests/dap.sh`), and `io::make` anywhere in the
file is the error "main already has the io". The C forms (`i32 main(i32 argc, char **argv)` in a module, `char *argv[]` too in a file with no `module` line, and `int main(void)`) lower
as they always did, and `.c` files are never rewritten. Tests: the cases `main-io*` and `error-main-io-make`.

**Array parameters keep their size in a module** (issue #81). Valid C in a file with no `module` line means what it means in
C, so only a file with a `module` line is changed, and each change is a rejection. A parameter `T xs[N]` is N objects:
the lowering writes it `T xs[static N]` in the code (`smith_lang_array_size`, `runtime/lang_head.c` for a held head,
`smith_lang_head_step` for a private one; the generated header keeps `T xs[N]`), which clang turns into a non-null,
`dereferenceable(N * sizeof(T))` pointer that `smith prove` reads (#74's `[static N]` mechanism, no second one). A parameter
with no size is an error at its declaration. `countof(xs)` is rewritten, in the module's bodies (`smith_lang_countof_parameter`,
`runtime/lang_sparse.c`), to the parameter's own size tokens (kept by `smith_lang_array_token` as the head streams by,
three dimensions of four tokens, eight parameters per function); the host watches `countof` once a function has one.
The other rejections are about the AST, so they cannot be tokens: `ArrayParameters` (`shim/hook.cpp`) walks the functions
the module defines when the translation unit is complete (the hook is Sema's external source and does its work when
Sema asks for the tentative definitions, at the end of the unit: no destructor of Sema's is called with `-disable-free`),
reading `ParmVarDecl::getOriginalType`, which clang keeps under the pointer: a constant index out of range, `++`, `--` and
assignment of the parameter, `sizeof(xs)`, and an argument to a module's function (declared in the file or in a
generated header; a C function takes the pointer) that is a pointer or an array of another shape. Tests: the cases
`array-param*` and `error-array-param-*`, and section 11a of `tests/prove.sh`. In an editor (`smith lsp`) the AST checks
depend on clangd reaching the end of the unit.

`smith_object` takes a `.smith` source like any other (knowledge/build-file.md). For each module it adds a
step that writes the module's header (`smith-cc --smith-header src/a/b.smith -o .smith/include/a/b.h`, restat: the file
is written only when its bytes change); every `.smith` compile gets `-I .smith/include` and waits for all the headers
and for one check, which reads each header's `// smith-module:` and `// smith-imports:` lines and fails on an import
cycle, naming it, before any compile starts. What an importer depends on is its depfile, which lists the headers it
read. So an edit to a module's body recompiles that module alone, and an edit to its exports recompiles its importers.
The rule that a module `a::b` lives in a file whose path ends in `a/b.smith` is checked once, when build.c runs.

`smith_tests` finds tests in `.smith` files (`export void test_x(void) {` in a module, `void test_x(void) {` in a file with
no `module` line), and the runner and its reports name them as written: `geometry::box_test::area`, and a failure shows the
`.smith` line, its source and `geometry::box_test::test_area_fails` in the chain (knowledge/build-tests.md). `smith prove`
compiles a `.smith` file as `smith cc` does and names its functions, calls and counterexample values as written
(`geometry::box::clamp`). Frames of a library written as the modules `smith::test`, `smith::contract` and `smith::trace`
are machinery in a report, as `smith_test_*` is. A module named `smith::test` itself writes its header to
`.smith/include/smith/test.h`, the path of the C library's header, so the library cannot become that module until the header
path is settled.

- A header imported with `import <h>;` is included textually; the importing file's macros reach it.
- A module's header is written without macros: a macro in an exported signature is not seen by it. An export under `#if`
  works (the directives are copied), but `#define`s are not, and the header pass does not evaluate a condition.
- A `c::NAME` that is a macro and an argument of another macro (`c::assert(n < c::SIZE_MAX)`) is expanded with the
  argument, before the lowering sees it: the lowering drops the `c::` before an expansion and the macro is allowed
  because the text before it says `c::` (`tests/smith-lang/cases/c-macro-argument`). One that expands to nothing
  there is not understood. `static_assert`, `alignas`, `alignof`, `bool`, `true`, `false`, `nullptr` and `thread_local`
  are keywords of C23 and need no `c::` even where a header defines them as macros (`<assert.h>` does `static_assert`).
- Variables declared with a struct in one declaration (`export struct S {…} s;`) are not given a symbol.
- A hover in an importer shows the declaration as the header gives it (`int32_t foo::f(int32_t x)`, from the header's
  standard C types, where the module says `i32`); the `asm("_foo::f")` label of the symbol is left out (`shim/lsp.cpp`).
- A rename of an exported name edits what clangd knows of (the module, and the importers its index has seen); it does
  not edit the generated headers. Like any cross-file rename in clangd it misses an importer the index has not read yet.
- A fix-it that a mistyped `x::name` offers puts in the member alone (`f`), so it makes `foo::f`.
- A note into an interface read in memory (no database) shows, in `smith cc`, that interface's line under the caret.
- `-x smith` is not a clang language: the `.smith` extension and `-x c` stand for it.
- `clang -E` and `-Xclang -dump-tokens` print the source text under a token the lowering made or renamed (it keeps the
  location of the token it replaces, so diagnostics, the debugger and clang-tidy's fixes point at the `.smith` text);
  the parser, the code and the debug info are unaffected. `SMITH_LANG_DUMP` shows the macro arguments of a call after
  the call, and `smith cc --smith-lower` is the lowering of the text as written.
- On macOS a backtrace names an export `_game::replay::open`, since the label is used as written (the underscore is the
  Mach-O prefix of a C symbol); `b game::replay::open` finds it.

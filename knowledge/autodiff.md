---
type: architecture
title: Enzyme is linked into smith's lld, and smith::autodiff is its interface
description: An autodiff program links with full LTO and no passes, runs AddressSanitizer after Enzyme in debug and has no UBSan.
tags: [autodiff, enzyme]
---
# Enzyme is linked into smith's lld, and smith::autodiff is its interface

**Enzyme (automatic differentiation of LLVM IR) is linked into smith's lld, and `smith::autodiff` is its interface**
(skill smith-autodiff). Enzyme v0.0.303 (Apache-2.0 with the LLVM exception, pinned by hash in build.c) is built static by
`smith build llvm` (`build_enzyme_make` in `build.c`, after the prover's libraries; `smith build enzyme` alone) into its own folder
`~/.cache/smith/enzyme/<version>-<LLVM configuration>/`, never inside an LLVM install, so building it changes no
configuration; `patches/enzyme/*.patch` go into a copy of its package (one: Enzyme printed `caching call` to stderr
unconditionally). The full smith links `libEnzymeStatic-23.a`, built with `-DENZYME_RUNPASS` (else it hooks no
pipeline); the stage0 has none. **Registration:** lld's LTO config has no PassBuilder hook, only `--load-pass-plugin`,
so `shim/lld.cpp` adds `--load-pass-plugin=<smith's own path>` and Enzyme's `llvmGetPassPluginInfo` is exported from smith
(`-exported_symbol`): lld dlopens the executable itself, so nothing is read from disk. A link with no bitcode never
reads the option, and a link with no Enzyme call costs nothing measurable. **The library** is `lib/smith/autodiff.smith`
(`__enzyme_autodiff`, `__enzyme_fwddiff`, the `enzyme_*` markers as `extern` and never defined; a source that defines
nothing, like math) **and `gradient`, `map` and `zeros`**, ordinary generics (issue #84; the structs are walked by `fields(T)`, never by a builtin). `gradient(arena, constexpr auto f, auto x, auto... rest)` (result `typeof(*x)`) starts from `zeros(arena, *x)`, a copy of `*x` in the shape of a gradient (`shadow_walk`: numbers zeroed, each `[:]` field given a new block of the same length from the arena, a `[:]` or fixed array of structs walked element by element, a pointer field a `static_assert` that says to use `[:]`), and calls the raw reverse mode as `dup x, constant rest..., interleave, &dx` (`enzyme_interleave`: one `enzyme_const` covers the whole run; with no extra argument the `constant` is left out by `if constexpr`, else it would take `interleave` as its argument); Enzyme sees a shadow struct whose slice pointers point at arena blocks, as a hand-built shadow would. It checks `f` with `sizeof(f(x, rest...))` so a wrong call is an error at the user's call. `map(f, out, rest...)` is the walk over the float leaves of any number of structs and numbers (issues #123, #124): one walker, `map_walk(f, n, dst, rest...)`, for every list. A struct pointer in `rest` is walked and any other argument is passed as it is, each by a helper in the pattern of an expansion: `map_at(rest, k)...` (the leaf `x[k]` of a pointer, the value itself of a number; result type `leaf_of(typeof(x))`, the user-written type function `static type leaf_of(type t) { if (is_pointer(t)) return typeof(*(t)nullptr); return t; }` of #143), `map_off(rest, k)...` (a pointer moved on by `k` values) and `map_down(like, rest, i, n)...` (a pointer taken into field `i` of its struct: `&p->f`, `&p->f[0]` for a fixed array, `p->f.data` for a slice with its length checked against `out`'s; `like` is the same pointer for `out`, which gives the result type through `down_of(typeof(x), typeof(like))`, since a type function cannot read a field; `i` is a `constexpr usize`, the loop variable of the walker's `for constexpr`). The walker reads `fields(typeof(*dst))`, so it carries no copy of the struct. The pointee of a pointer in a type function is known one level deep (#143), enough for a struct pointer. Their bodies write the module's own names bare (`reverse`, `dup`): `autodiff::dup` would not resolve in the instance. The type functions stand above the Enzyme marker declarations: a type function written after an `export extern` variable is not read (#171). **What stays** (language, not library): a generic function cannot be passed as `f` (`f` is a plain function, so an `f32` leaf is converted to its `f64` parameters and back). The walker's `out` parameter is named `dst` because the module exports the Enzyme marker `out`. C has none of them (no generics), only the raw interface. **A program that reaches it is an autodiff program** (`smith_app`'s scan finds the library as it
does every other) and, in every profile and for the tests, links with `-flto=full` (ThinLTO fails), compiles each file with
`-Xclang -disable-llvm-passes` (Enzyme wants IR not yet optimized; the link optimizes after it), puts its objects in
`build/<profile>/autodiff/`, and **debug has AddressSanitizer, run
after Enzyme, and no UBSan** (owner's decision): debug compiles with `-fsanitize=address -fno-sanitize-recover=address`
and no passes, so functions carry `sanitize_address` uninstrumented, and `shim/lld.cpp`'s plugin entry (smith's own,
Enzyme's is weak) registers Enzyme and then ASan at the same full-LTO early extension point, so ASan instruments the
gradients too (it skips a module that already has `asan.module_ctor`, so it never runs twice); the link keeps lld's
object (`-Wl,-object_path_lto`) so reports have lines. UBSan's handlers break Enzyme and its trap mode loses its checks
in the gradient (#59 tracks UB checks after Enzyme). Release and fast have none. `smith_autodiff_flags` in
runtime/build_runtime.c is the one place. Programs that do not use it have unchanged commands. A smith with no Enzyme, or
run with `SMITH_CLANG` or `SMITH_LINKER`, refuses an autodiff program (`SMITH_ENZYME=no` in the build program's
environment). `smith cc` adds no flag: `-flto=full` through smith's lld differentiates.

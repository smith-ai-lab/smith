---
name: smith-lang-target
description: "The target and if constexpr in .smith, which has no preprocessor: import target; target::arch, os, assertions, address_sanitizer and the rest, what replaces #if and #define, and the error a #define gets. Load when a .smith file needs the platform, a sanitizer flag, a constant, or a compile-time branch."
---

# No preprocessor: the target and `if constexpr`

A `.smith` file needs no `#if`. A fact of the target is a constant of the module `target` (no file; `import target;` first); a choice between code that may not compile everywhere is `if constexpr`:

```smith
import target;

constexpr Usize size = 4;                                   // not `#define SIZE 4`
constexpr Bool sanitized = target::address_sanitizer;       // a constant expression: static_assert, array sizes, initializers
static_assert(target::f32_mantissa_bits == 24 && target::float_radix == 2);

I32 opaque(I32 value) {
    if constexpr (target::arch == target::aarch64) {        // the branch not taken is dropped: it may not compile here
        __asm__("" : "+w"(value));
    } else {
        __asm__("" : "+X"(value));
    }
    return value;
}

Usize capacity() {
    return size;
}

Bool poisons_memory() {
    return sanitized;
}
```

The facts: `arch` (`aarch64`, `x86_64`), `os` (`macos`, `linux`), `address_sanitizer`, `undefined_sanitizer`, `thread_sanitizer`, `assertions` (true unless `NDEBUG`; smith's own checks ignore it), `float_radix`, `f32_mantissa_bits`, `f64_mantissa_bits`, `float_eval_method`, `static_analyzer` (true only while clang-tidy's analyzer runs).

## Rules

- The condition of `if constexpr` is `target::` facts, `constexpr` constants (the file's or the function's) and literals, with C's operators; branches are blocks (`else if constexpr (...) { }` continues a chain). The branch not taken is never parsed: no diagnostics and no hover in it.
- Facts cannot be in an exported declaration, but the body of an exported `inline` function may ask them: the header gets the `#if` (`std::contract` does).
- At file scope, `if constexpr` chooses declarations and imports for the target (`smith-lang-if-constexpr`).
- `smith cc --smith-lower` has no preprocessor, so it writes the `SMITH_TARGET_ARCH` macros and `#if`/`#else`/`#endif` for the branches: the preprocessor is only in generated output.
- In a module (a file with a `module` line) the macros the target predefines under a plain name (`linux`, `unix`, `i386`) are undefined, so `import std::clock::linux;` and a local named `unix` keep their names; `__linux__` and the command line's `-D` stay, and a file with no `module` line keeps C's meaning.
- In a generic, `if constexpr` also reads types (`smith-lang-generics`).

## Errors

- ``in `if constexpr`: `n` is not a constant``: anything else in a condition, at the name.
- `smith lint`, check `smith-preprocessor`: `` `#define` is the preprocessor, and a .smith file has none: a constant is `constexpr Usize name = value;` ``. A macro of a C header used as `c::EOF` compiles but is reported the same way (`smith-lang-c-interop`).

Details: [smith-language](../../../knowledge/smith-language.md). Overview: `smith-lang`.

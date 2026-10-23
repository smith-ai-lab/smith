---
name: smith-lang-target
description: "The target and if constexpr in .smith, which has no preprocessor: import target; target::arch, os, assertions, address_sanitizer and the rest, and what replaces #if. Load when a .smith file needs the platform, a sanitizer flag or a compile-time branch."
---

# The target and if constexpr

## The target and `if constexpr`: no preprocessor in `.smith`

A `.smith` file needs no `#if`. A fact of the target the file is compiled for is a constant of the module `target` (a module that is no file; `import target;` first), and a choice between code that may not compile on every target is `if constexpr`:

```smith
import target;

constexpr bool sanitized = target::address_sanitizer;       // a constant expression: static_assert, array sizes, initializers
static_assert(target::f32_mantissa_bits == 24 && target::float_radix == 2);

i32 opaque(i32 value) {
    if constexpr (target::arch == target::aarch64) {        // the branch not taken is dropped: it may not compile here
        __asm__("" : "+w"(value));
    } else {
        __asm__("" : "+X"(value));
    }
    return value;
}

bool poisons_memory(void) {
    return sanitized;
}
```

The facts: `arch` (`aarch64`, `x86_64`), `os` (`macos`, `linux`), `address_sanitizer`, `undefined_sanitizer`, `thread_sanitizer`, `assertions` (true unless `NDEBUG`), `float_radix`, `f32_mantissa_bits`, `f64_mantissa_bits`, `float_eval_method`, `static_analyzer` (true only while clang-tidy's analyzer runs). The condition of `if constexpr` is made of `target::` facts, `constexpr` constants (of the file or the function) and literals, with the operators of C; the branches are blocks (`else if constexpr (...) { }` continues a chain). Anything else in a condition is an error at the name (``in `if constexpr`: `n` is not a constant``). The branch not taken is never parsed: no diagnostics and no hover in it. Facts cannot be in an exported declaration, but the body of an exported `inline` function may ask them with `if constexpr`: the header gets the `#if` (smith::contract does). `smith cc --smith-lower` has no preprocessor, so it writes macros (`SMITH_TARGET_ARCH`, defined at the top by `#if defined(__aarch64__)`) and `#if`/`#else`/`#endif` for the branches: the preprocessor is only in generated output. Details: [smith-language](../../../knowledge/smith-language.md).

Also in this family: [smith-lang](../smith-lang/SKILL.md).

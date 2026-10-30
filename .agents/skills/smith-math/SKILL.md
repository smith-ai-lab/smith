---
name: smith-math
description: "Using smith::math, smith's numbers as generic functions of a type: math::is_finite, math::sqrt, math::abs, and the constants math::max(usize), math::infinity(f64), math::nan(f32). Load when a .smith file imports smith::math, when a build.c calls smith_library(b, \"math\"), when smith lint says `c::SIZE_MAX`, `c::isfinite`, `c::INFINITY` or `c::NAN` is a C macro, or on a question about square roots, infinity, NaN or the largest integer in .smith without <math.h> macros."
---

# Numbers as functions of a type: smith's math

This skill ships in the smith plugin, from `.agents/skills/smith-math/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there.

`smith::math` (`lib/smith/math.smith`) holds what a `.smith` file used to reach through C's macros of `<math.h>` and `<stdint.h>`: a `c::` name that is a macro is the preprocessor, and `smith lint` (`smith-preprocessor`) refuses it. Every export is a **generic function** built on clang's builtins (a builtin is a function, not a macro), so it has no C spelling: the header holds only its record, and C keeps `<math.h>`. It holds only what smith's own libraries ask today; the first caller that needs another number function adds it.

```smith
import smith::math;

f64 length(f64 x, f64 y) {
    return math::sqrt(x * x + y * y);          // the square root, correctly rounded: libm's sqrt, as an instruction
}

bool usable(f32 value) {
    return math::is_finite(value) && math::abs(value) < 1000.0f;  // the type is the type of the argument
}

usize count_limit() {
    return math::max(usize);                         // SIZE_MAX
}

f64 missing(bool negative) {
    return negative ? -math::infinity(f64) : math::nan(f64);  // INFINITY and NAN, in the type asked
}
```

| Call | Replaces | Meaning |
|---|---|---|
| `math::is_finite(x)` | `c::isfinite` | neither infinite nor not-a-number; `x` is `f32` or `f64` |
| `math::sqrt(x)` | `c::sqrt`, `c::sqrtf` | the square root, correctly rounded |
| `math::abs(x)` | `c::fabs`, `c::fabsf` | the absolute value (the sign bit cleared) |
| `math::max(usize)` | `c::SIZE_MAX` | the largest value of an unsigned integer type |
| `math::infinity(f64)` | `c::INFINITY` | positive infinity of a floating type; negate it for the other |
| `math::nan(f32)` | `c::NAN` | a quiet not-a-number of a floating type |

- **`is_finite`, `sqrt` and `abs` take `auto x`**: the type is the argument's (a name, a field chain, `*e`, `e[i]`, a call, a cast, a literal, or arithmetic over them: `math::is_finite(high - low)`), and `sqrt` and `abs` return it (`export typeof(x) sqrt(auto x)`). The constants carry no argument, so they take the type where a value goes (`math::max(usize)`). A module read by the raw lowering (`smith cc --smith-lower`, the headers) cannot call the `auto` functions, because the lowering without a compiler cannot type the argument: it calls the builtins they wrap (`__builtin_isfinite(x)`, `__builtin_elementwise_sqrt(x)`, `__builtin_elementwise_abs(x)`).
- **They are always-inline functions**, so the optimizer folds `math::max(usize)` to the literal. A generic call is not a constant expression in C, so in a `static_assert` or an array size write the number or a `constexpr` of the file. What is a constant is a `target::` fact (`target::f64_mantissa_bits`), which the lowering writes as a literal.
- **Get the library in build.c** with `smith_library(b, "math")` like any library; it has no symbols of its own, only the instances a caller makes (weak and hidden), so nothing is linked for it. A module that imports it needs nothing more.

---
name: smith-lang-unused
description: "`_` and `_name` in .smith modules: a name that is unused on purpose (`void on_key(Window *_window, Key key)`, `i32 pick(i32 _, i32 _, i32 third)`), `_ = f();` to discard a value, and the error when a `_` name is read. Load when a parameter or local is unused, when discarding a [[nodiscard]] result, or on '`_x` is marked unused (it starts with `_`)'."
---

# Unused on purpose: `_` and `_name`

In C, `(void)x;` and `[[maybe_unused]]` say a value is not used. In a `.smith` file with a `module` line a name that is `_`, or starts with `_`, is never read:

```smith
module main;

import <stdio.h>;

[[nodiscard]] i32 compute(i32 x) {
    return x * 2;
}

i32 pick(i32 _, i32 _, i32 third) {      // several `_` in one scope
    return third;
}

void on_key(i32 *_window, i32 key) {     // `_window` keeps the name as documentation
    c::printf("key %d\n", key);
}

i32 main() {
    i32 window = 0;
    _ = compute(3);                      // discards a value, a [[nodiscard]] result too
    on_key(&window, pick(1, 2, 3));
    return 0;
}
```

## Rules

- **`_` declares nothing you can name.** A parameter or a local called `_` may repeat in a scope; it is never read (``error: `_` is marked unused``).
- **`_ = expr;` is a statement of its own** that evaluates `expr` and drops it (it replaces `(void)expr;`, which stays valid C). Put it in braces after an `if` or `else`; there is no variable `_` to assign twice.
- **`_name` is a name you can declare but not read.** ``error: `_window` is marked unused (it starts with `_`); rename it to use it``. The compiler does not warn that it is unused.
- **Not these:** C's own names (`__x`, `_X`), a member (`p._x`, `p->_x`) and a name after `::` (`c::_exit`) are no module's.
- **Without a `module` line nothing changes:** `int _ = 1; return _;` is C.

Overview: `smith-lang`. Reference: [knowledge/smith-language.md](../../../knowledge/smith-language.md) ("`_` and `_name`").

---
name: smith-format
description: "Using smith::format (import smith::format; format::to, format::push, format::length, format::hex, format::fixed, format::general, format::scientific, format::pad, format::left, format::bytes): a type-checked formatter in place of snprintf, the pieces of the text as arguments. Load when a .smith file imports smith::format, when a build.c calls smith_library(b, \"format\"), on writing text into a buffer or an arena in .smith, on replacing snprintf or printf, or on 'no rule for the type of an argument'."
---

# Formatted text with no format string: smith::format

This skill ships in the smith plugin, from `.agents/skills/smith-format/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. It uses the generics of `smith-lang-generics` and `smith-lang-rest`, and `smith-text-numbers` and `smith-arena`.

There is no format string, so none can disagree with its arguments: the arguments are the pieces, in order, and each one's **type picks how it is written, when the call is compiled**. Write `format::to(buf, sizeof buf, "count: ", n, " of ", total)`, never `snprintf(buf, sizeof buf, "count: %d of %zu", ...)`.

```smith fragment
import smith::format;
import smith::mem;

char out[128];
format::Written w = format::to(out, sizeof out, "move ", dx, " ", dy, " key ", format::hex(key, 16));
if (!w.fits) { /* cut: out is terminated, w.length is what the whole text needs */ }
char path[:] = format::push(folder, "/", name, ".mesh");   // path.data is terminated too (not counted)
usize n = format::length("abc", 12345);                               // 8, writes nothing
```

| Argument | Written as |
|---|---|
| `bool` | `true` / `false` |
| `char` | the character (`i8` and `u8` are numbers) |
| `i8`..`i64`, `u8`..`u64`, `usize`, `isize` | decimal digits, `-` for a negative one |
| `f32`, `f64` | the shortest digits that read back the same (`0.1`, `1e22`), as `text::write_f64`; `inf`, `-inf`, `nan`; a `f32` as its `f64` value |
| `char *`, `const char *` | the terminated string; null is `(null)` |
| `char[:]`, `const char[:]` | its count of characters (`%.*s`); `format::bytes(p, n)` makes one from a pointer and a length |
| `format::hex(x, width)` | lowercase hex, zero-padded to `width` (0 for none, at most 16): `%016llx`, `%02x` |
| `format::fixed(x, places)` | `places` digits (at most 20) correctly rounded, ties to even, as `%.Nf`; a value of 2^63 or more as with no wrapper |
| `format::general(x, digits)` | `%.Ng`: `digits` significant digits (1 to 40), exact rounding, fixed or `e` style by the exponent, trailing zeros removed: `0.1`, `1e+22`, `1e-05` |
| `format::scientific(x, places)` | `%.Ne`: `1.23e+04`, `places` at most 40 |
| `format::pad(piece, width)`, `format::left(piece, width)` | any piece above (not another pad) in a column of `width` characters, spaces before it or after it: `%5d`, `%12s`, `%-16s`, `%6.1f` as `pad(format::fixed(x, 1), 6)`; a wider piece is written whole; a string or `Text` must outlive the call that writes it; the text of any other piece is held (at most 63 characters) |

- **A struct** is written `{m = 1, b = 2}`: each field by the rule of its type, a nested struct the same way, a fixed array as `[1, 2, 3]` and a `char` array as its text (the fields come from `fields(T)`, `smith-lang-reflection`); a union, a struct with an anonymous member and a pointer field are errors. **A type with no rule** (a pointer, an enum) is a compile-time error at the call that names the piece: `static assertion failed: format: no rule for the type of piece 3; the note lists the types in order`, with the notes `in instance smith::format::no_rule(char [ 2 + 1 ])` (the piece's number is the array size) and `in instance smith::format::to(char *, int, Point, int)` (the types in order). The expression is not named (one instance serves every call with those types). Convert it: `(u64)(usize)p`, `(i64)kind`, or the field.
- **The console** is `console::print("n = ", n, "\n")` and `console::print_error` (skill smith-console): the same pieces, no length limit; `format` has no print.
- **Writing never stops.** `to(buffer, capacity, ...)` cuts the text where the buffer ends and always terminates it when `capacity > 0`; `Written.length` is the whole text's length (as `snprintf` returns) and `fits` is `length < capacity`. `capacity` 0 with a null buffer only measures. `push` measures, then pushes exactly `length + 1` bytes, so a full arena stops as any push does.
- **Up to 30 pieces** in one call (a generic takes 32 parameters).
- **printf to pieces**: `%-34s %10u %5.1f%%` is `format::left(label, 34), " ", format::pad(a, 10), " ", format::pad(format::fixed(x, 1), 5), "%"`; `%.9g` is `format::general(x, 9)`; `%016llx` is `format::hex(x, 16)`. There is no zero-padded decimal (`%03d`): pad with `hex`, or write the zeros.
- **C cannot call the generics.** C keeps `snprintf`, and gets the pieces `to` is made of from `<smith/format.h>`: `SmithFormatWriter w = smith_format_make(buf, cap); smith_format_put_string(&w, "n="); smith_format_put_u64(&w, n); SmithFormatWritten r = smith_format_finish(&w);` (also `put_char`, `put_bytes`, `put_i64`, `put_f64`, `put_hex`, `put_fixed`, `put_general`, `put_scientific`, `put_padded`; `smith_format_hex`, `smith_format_fixed`, `smith_format_general`, `smith_format_scientific` and `smith_format_bytes` make the wrappers).
- **Get the library** with `smith_library(b, "format")`, plus `mem` and `text`, which it imports; a project built by convention needs nothing more.

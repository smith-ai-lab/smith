---
name: smith-format
description: "Using smith::format (import smith::format; format::to, format::push, format::length, format::hex, format::fixed, format::bytes): a type-checked formatter in place of snprintf, the pieces of the text as arguments. Load when a .smith file imports smith::format, when a build.c calls smith_library(b, \"format\"), on writing text into a buffer or an arena in .smith, on replacing snprintf or printf, or on 'no rule for the type of an argument'."
---

# Formatted text with no format string: smith::format

This skill ships in the smith plugin, from `.agents/skills/smith-format/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. It uses the generics of `smith-lang-generics` and `smith-lang-lists`, and `smith-text-numbers` and `smith-arena`.

There is no format string, so none can disagree with its arguments: the arguments are the pieces, in order, and each one's **type picks how it is written, when the call is compiled**. Write `format::to(buf, sizeof buf, "count: ", n, " of ", total)`, never `snprintf(buf, sizeof buf, "count: %d of %zu", ...)`.

```smith fragment
import smith::format;
import smith::arena;

char out[128];
format::Written w = format::to(out, sizeof out, "move ", dx, " ", dy, " key ", format::hex(key, 16));
if (!w.fits) { /* cut: out is terminated, w.length is what the whole text needs */ }
text::Text path = format::push(&arena, folder, "/", name, ".mesh");   // path.begin is terminated too
usize n = format::length("abc", 12345);                               // 8, writes nothing
```

| Argument | Written as |
|---|---|
| `bool` | `true` / `false` |
| `char` | the character (`i8` and `u8` are numbers) |
| `i8`..`i64`, `u8`..`u64`, `usize`, `isize` | decimal digits, `-` for a negative one |
| `f32`, `f64` | the shortest digits that read back the same (`0.1`, `1e22`), as `text::write_f64`; `inf`, `-inf`, `nan`; a `f32` as its `f64` value |
| `char *`, `const char *` | the terminated string; null is `(null)` |
| `text::Text` | its length of characters (`%.*s`); `format::bytes(p, n)` makes one |
| `format::hex(x, width)` | lowercase hex, zero-padded to `width` (0 for none, at most 16): `%016llx`, `%02x` |
| `format::fixed(x, places)` | `places` digits (at most 20) correctly rounded, ties to even, as `%.Nf`; a value of 2^63 or more as with no wrapper |

- **A type with no rule** (a pointer, an enum, a struct) is a compile-time error at the call: `static assertion failed: format: no rule for the type of an argument`, with the note `in instance smith::format::to(char *, Point)` naming the types. Convert it: `(u64)(usize)p`, `(i64)kind`, or the field.
- **Writing never stops.** `to(buffer, capacity, ...)` cuts the text where the buffer ends and always terminates it when `capacity > 0`; `Written.length` is the whole text's length (as `snprintf` returns) and `fits` is `length < capacity`. `capacity` 0 with a null buffer only measures. `push` measures, then pushes exactly `length + 1` bytes, so a full arena stops as any push does.
- **Up to 30 pieces** in one call (a generic takes 32 parameters).
- **No padding, `%g` or `%e`**: columns and scientific text are not asked for yet; `fixed` and the default float cover the callers.
- **C cannot call the generics.** C keeps `snprintf`, and gets the pieces `to` is made of from `<smith/format.h>`: `SmithFormatWriter w = smith_format_make(buf, cap); smith_format_put_string(&w, "n="); smith_format_put_u64(&w, n); SmithFormatWritten r = smith_format_finish(&w);` (also `put_char`, `put_bytes`, `put_i64`, `put_f64`, `put_hex`, `put_fixed`; `smith_format_hex`, `smith_format_fixed` and `smith_format_bytes` make the wrappers).
- **Get the library** with `smith_library(b, "format")`, plus `arena` and `text`, which it imports; a project built by convention needs nothing more.

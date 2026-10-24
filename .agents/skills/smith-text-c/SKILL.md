---
name: smith-text-c
description: "smith::text from C (#include <smith/text.h>) and compiling it in build.c with smith_library(b, "text"). Load when C code includes <smith/text.h> or a build.c compiles text."
---

# text from C

`#include <smith/text.h>`; text is the generated slice struct, `struct smith_array_const_char_n` for read-only text (`.data`, `.count[0]`; `struct smith_array_char_n` is the writable one, the same with `char *data`), written `{ .data = p, .count = {n} }`; there is no `SmithText`. Every function is `smith_text_<name>` for every function (`smith_text_make`, `smith_text_take_line`, `smith_text_parse_f32`, `smith_text_write_u64`...), with the capacities `smith_text_number_capacity` and `smith_text_f64_capacity`. The same loop:

```c
#include <smith/text.h>

struct smith_array_const_char_n text = {.data = file.data, .count = {file.size}};   // or smith_text_make("a terminated string")
struct smith_array_const_char_n line, word;
while (smith_text_take_line(&text, &line)) {
    if (!smith_text_take_word(&line, &word)) continue;
    if (smith_text_is(word, "turn")) {
        float degrees;
        uint64_t ticks;
        if (!smith_text_take_word(&line, &word) || !smith_text_parse_f32(word, &degrees)) { /* report the line */ }
        if (!smith_text_take_word(&line, &word) || !smith_text_parse_u64(word, &ticks)) { /* report the line */ }
    }
}
char out[smith_text_number_capacity];
size_t length = smith_text_write_u64((struct smith_array_char_n){.data = out, .count = {sizeof out}}, tick);
```

## Get the library in build.c

text needs nothing else. smith writes the header and the source (`.smith/lib/smith/text.smith`) into `.smith/` on every `smith build`; `smith_library(b, "text")` returns the source's path and `smith_object` compiles a `.smith` source as it does a `.c` (the header step and `-I .smith/include` are added for it):

```c
SmithObject text = {.compiler = "smith-cc", .source = smith_library(b, "text"), .output = "build/smith/text.o", .flags = flags};
smith_object(b, &text);
```

`flags` includes `-I .smith/include`, and the program's link and `.objects` include it. Tests of code that uses it link it in `smith_tests`' `.objects`. See the smith-build and smith-test skills.

Also in this family: [smith-text](../smith-text/SKILL.md), [smith-text-numbers](../smith-text-numbers/SKILL.md).

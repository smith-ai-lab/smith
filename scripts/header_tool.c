// The writer of the .smith libraries' headers, standalone: `header_tool lib/std/arena.smith -o arena.h` is
// `smith cc --smith-header lib/std/arena.smith -o arena.h` without a smith, a clang or an LLVM. The writer is a
// plain lexer and the lowering (runtime/lang*.c), which this file includes as smith.c does; a C compiler builds it:
//   cc -std=gnu23 -x c scripts/header_tool.c -o header_tool
// It is how scripts/seed_linux.c, which has no smith, makes the headers that smith.c's compile needs; a test
// (tests/header_tool_test.smith) builds it with the host compiler and checks its output against `smith cc
// --smith-header` for every library, so it cannot rot. It answers no question to a compiler (smith_lang_ask_compiler),
// so a module that imports several C headers has them all in its header, where smith writes only those its exports use;
// both are valid headers.
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>

#include "../runtime/base.h"
#include "../runtime/lang.h"

// The compiler is not asked: the header keeps every import.
static bool smith_lang_ask_compiler(const SmithLang *lang, const char **spellings) {
    (void)lang;
    (void)spellings;
    return false;
}

#include "../runtime/lang.c"
#include "../runtime/lang_lex.c"

int main(int argc, char **argv) {
    return smith_lang_main(argc - 1, argv + 1, true);
}

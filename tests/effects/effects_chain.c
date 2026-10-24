// A function marked pure that reaches printf through two calls: the error names the chain.
#include <stdio.h>

static void log_it(const char *text) {
    printf("%s\n", text);
}

static void helper(const char *text) {
    log_it(text);
}

[[smith::pure]] int bad(const char *text) {
    helper(text);
    return 0;
}

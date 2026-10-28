// The report-only mode of the pointer rule (#396): SMITH_EFFECTS_POINTERS lists each call through a pointer with the
// effects its function type states (`-` when it states none, so it is `c`) and each function made into a pointer.
#include <stdio.h>

typedef void (*Logger)(const char *) [[clang::annotate_type("smith::effects:console")]];

struct Hooks {
    int (*step)(int) [[clang::annotate_type("smith::effects:")]];
    int (*other)(int);
};

static void say(const char *text) {
    puts(text);
}

static int twice(int x) {
    return x * 2;
}

int run(Logger log, struct Hooks hooks, int (*plain)(int)) {
    log("a");
    int a = hooks.step(1);
    int b = hooks.other(2);
    return a + b + plain(3);
}

int use(void) {
    struct Hooks hooks = {twice, twice};
    return run(say, hooks, twice);
}

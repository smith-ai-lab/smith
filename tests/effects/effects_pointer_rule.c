// The pointer rule (#396): a function pointer type states an upper bound, checked where a function is put in it and
// relied on where it is called. C writes the type attribute as clang's annotate_type.
#include <stdio.h>
#include <stdlib.h>

typedef void (*Logger)(const char *) [[clang::annotate_type("smith::effects:console")]];
typedef int (*Step)(int) [[clang::annotate_type("smith::effects:")]];

[[smith::effects(console)]] static void say(const char *text) {
    puts(text);
}

static int twice(int x) {
    return x * 2;
}

static int noisy(int x) {
    printf("%d\n", x);
    return x;
}

static int helper(int x) {
    return noisy(x);
}

struct Sink {
    Logger log;
    Step step;
};

static int apply(Step step, int x) {
    return step(x);
}

[[smith::effects(console)]] static void ok(void) {
    Logger log = say;
    Step step = &twice;
    struct Sink sink = {.log = say, .step = twice};
    log("a");
    sink.log("b");
    printf("%d\n", apply(step, 1) + sink.step(2));
}

static void bad_initializer(void) {
    Step step = noisy;
    (void)step;
}

static void bad_assignment(Step slot[1]) {
    slot[0] = helper;
}

static void bad_list(void) {
    struct Sink sink = {.log = say, .step = noisy};
    (void)sink;
}

static int bad_argument(void) {
    return apply(helper, 3);
}

static Step bad_return(void) {
    return noisy;
}

typedef void (*Quiet)(const char *) [[clang::annotate_type("smith::effects:")]];

static void bad_pointer(Logger log) {
    Quiet quiet = log;
    (void)quiet;
}

// A call through a pointer has the bound's effects, not `c`.
[[smith::effects(console)]] static void relies(Logger log) {
    log("x");
}

[[smith::pure]] static int pure_call(Step step) {
    return step(1);
}

[[smith::pure]] static void console_call(Logger log) {
    log("y");
}

// A pointer without a bound is `c`.
[[smith::effects(console)]] static void unbounded(void (*log)(const char *)) {
    log("z");
}

Logger global_ok = say;
Step global_bad = noisy;

// Its own effects go before a function; after the parameter list is a pointer type's place.
static void after(const char *text) [[clang::annotate_type("smith::effects:")]] {
    puts(text);
}

// A pointer that states nothing is `c`: it does not fit a bound.
static void unknown(void (*any)(const char *)) {
    Logger log = any;
    Logger none = nullptr;
    (void)log;
    (void)none;
}

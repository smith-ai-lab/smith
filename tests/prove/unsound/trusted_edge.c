// False claims across a trusted boundary: a trusted function's body is not read, so its promise is the whole story,
// and a caller must not be given more than the promise says.
// unsound: relies_on_value ensures failed
// unsound: relies_on_unwritten_global ensures failed
// unsound: relies_on_reachable_memory ensures failed
#include <smith/contract.h>

#include <stdint.h>
#include <string.h>

// Promises nothing about its result.
int32_t pick(int32_t x) {
    smith_trusted();
    smith_requires(x >= 0);
    return x + 1;
}

int32_t relies_on_value(int32_t x) {
    smith_requires(x >= 0 && x < 100);
    int32_t y = pick(x);
    smith_ensures(y == x + 2);
    return y;
}

int32_t counter = 0;

// Writes a global, which no parameter points to, and says nothing of it.
void bump_global(int32_t *out) {
    smith_trusted();
    counter += 1;
    *out = 0;
}

int32_t relies_on_unwritten_global(int32_t *out) {
    counter = 0;
    bump_global(out);
    smith_ensures(counter == 0);
    return counter;
}

typedef struct {
    int32_t *target;
    int32_t value;
} Holder;

// Writes through a pointer inside its parameter: memory the parameter reaches, not the parameter itself.
void writes_behind(Holder *holder) {
    smith_trusted();
    *holder->target = 9;
}

int32_t relies_on_reachable_memory(Holder *holder, int32_t *other) {
    smith_requires(*other == 1);
    holder->target = other;
    writes_behind(holder);
    smith_ensures(*other == 1);
    return *other;
}

int main(int argc, char **argv) {
    if (argc < 2) return 0;
    const char *name = argv[1];
    int32_t out = 0, other = 1;
    Holder holder = {&out, 0};
    if (strcmp(name, "relies_on_value") == 0) (void)relies_on_value(1);
    if (strcmp(name, "relies_on_unwritten_global") == 0) (void)relies_on_unwritten_global(&out);
    if (strcmp(name, "relies_on_reachable_memory") == 0) (void)relies_on_reachable_memory(&holder, &other);
    return 0;
}

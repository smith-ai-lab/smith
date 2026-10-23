// False claims about what a call leaves alone: a caller may assume a callee's smith_ensures and nothing else it does.
// unsound: relies_on_untouched_byte ensures failed
// unsound: relies_on_untouched_field ensures failed
// unsound: relies_on_old_value ensures failed
// unsound: callee_ensures_wrong ensures failed
#include <smith/contract.h>

#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t buf[4];
    uint8_t len;
    uint8_t other;
} List;

static void push(List *list, uint8_t x) {
    smith_requires(list->len < 3);
    list->buf[list->len++] = x;
    list->buf[0] = 0; // overwrites what the caller wrote, and says nothing of it
    smith_ensures(list->len > 0);
}

uint8_t relies_on_untouched_byte(List *list) {
    smith_requires(list->len < 3);
    list->buf[0] = 9;
    push(list, 1);
    uint8_t first = list->buf[0];
    smith_ensures(first == 9);
    return first;
}

static void touches_other(List *list) {
    smith_requires(list->len < 3);
    list->other = 77;
    smith_ensures(list->len == list->len);
}

uint8_t relies_on_untouched_field(List *list) {
    smith_requires(list->len < 3 && list->other == 1);
    touches_other(list);
    smith_ensures(list->other == 1);
    return list->other;
}

static void bump(List *list) {
    smith_requires(list->len < 200);
    list->len = (uint8_t)(list->len * 2 + 1);
    smith_ensures(list->len > 0);
}

// The old length is gone after the call; only `> 0` is promised.
uint8_t relies_on_old_value(List *list) {
    smith_requires(list->len < 100);
    const uint8_t before = list->len;
    bump(list);
    smith_ensures(list->len == before + 1);
    return list->len;
}

// The function's own promise is false.
uint8_t callee_ensures_wrong(uint8_t x) {
    smith_requires(x < 200);
    uint8_t y = (uint8_t)(x + 2);
    smith_ensures(y == x + 1);
    return y;
}

int main(int argc, char **argv) {
    if (argc < 2) return 0;
    const char *name = argv[1];
    List list = {{0}, 0, 1};
    if (strcmp(name, "relies_on_untouched_byte") == 0) (void)relies_on_untouched_byte(&list);
    if (strcmp(name, "relies_on_untouched_field") == 0) (void)relies_on_untouched_field(&list);
    List old = {{0}, 1, 1};
    if (strcmp(name, "relies_on_old_value") == 0) (void)relies_on_old_value(&old);
    if (strcmp(name, "callee_ensures_wrong") == 0) (void)callee_ensures_wrong(1);
    return 0;
}

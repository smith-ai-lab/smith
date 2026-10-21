// A callee's smith_ensures about memory is assumed by its callers (issue #41): the callee writes through a pointer, and
// the caller reads what it wrote.
#include <smith/contract.h>
#include <stdint.h>

typedef struct {
    uint8_t buf[10];
    uint8_t len;
} List;

static void set_five(List *list) {
    list->len = 5;
    smith_ensures(list->len == 5);
}

// A local object: its bytes after the call are what the callee promised.
uint8_t five(void) {
    List list = {};
    set_five(&list);
    const uint8_t value = list.len;
    smith_ensures(value == 5);
    return value;
}

static void bump(List *list) {
    smith_requires(list->len < 200);
    list->len = list->len + 1;
    smith_ensures(list->len > 0);
}

// A pointer parameter; the caller's own write is overwritten by the callee's promise.
uint8_t three_then_bump(List *list) {
    smith_requires(list->len < 100);
    list->len = 3;
    bump(list);
    const uint8_t value = list->len;
    smith_ensures(value > 0);
    return value;
}

// The callee promises only that the length is above zero, so the caller cannot know it is 4.
uint8_t too_much(List *list) {
    smith_requires(list->len < 100);
    list->len = 3;
    bump(list);
    const uint8_t value = list->len;
    smith_ensures(value == 4);
    return value;
}

static void push(List *list, uint8_t x) {
    smith_requires(list->len < 10);
    const uint8_t old = list->len;
    list->buf[list->len++] = x;
    smith_ensures(list->len == old + 1);
}

// Two calls in a row: each one's promise is about the memory the one before left (this used to crash the prover).
uint8_t two(void) {
    List list = {};
    push(&list, 1);
    push(&list, 2);
    smith_ensures(list.len == 2);
    return list.len;
}

uint8_t two_through_a_pointer(List *list) {
    smith_requires(list->len + 2 <= 10);
    const uint8_t old = list->len;
    push(list, 1);
    push(list, 2);
    smith_ensures(list->len == old + 2);
    return list->len;
}

// A caller that relies on what push does not promise: that it leaves the first byte alone.
uint8_t relies_on_the_body(void) {
    List list = {};
    list.buf[0] = 9;
    push(&list, 1);
    smith_ensures(list.len == 1);
    const uint8_t first = list.buf[0];
    smith_ensures(first == 9);
    return first;
}

// The old value read twice by one contract (the same load reached from two places).
static void bump_twice_read(List *list) {
    smith_requires(list->len < 200);
    const uint8_t old = list->len;
    list->len = old + 1;
    smith_ensures(list->len == old + 1 && old < 255);
}

uint8_t uses_bump_twice_read(void) {
    List list = {};
    bump_twice_read(&list);
    smith_ensures(list.len == 1);
    return list.len;
}

static void clobber(List *list) {
    list->buf[0] = 3;
    list->len = 1;
    smith_ensures(list->len == 1);
}

// What the callee overwrote is not what the caller wrote, local object or parameter: a counterexample, never a proof.
uint8_t clobbered_local(void) {
    List list = {};
    list.buf[0] = 9;
    clobber(&list);
    const uint8_t first = list.buf[0];
    smith_ensures(first == 9);
    return first;
}

uint8_t clobbered_parameter(List *list) {
    list->buf[0] = 9;
    clobber(list);
    const uint8_t first = list->buf[0];
    smith_ensures(first == 9);
    return first;
}

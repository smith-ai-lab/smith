#include <stdio.h>

// A pointer is nullable in C, and `some` and `none` are ordinary names (issue #86): without a `module` line nothing
// about null pointers, uninitialized pointers or members left out of an initializer is an error, and a member named
// `some` is a member.
struct node {
    int value;
    struct node *next;
    struct node *some;
};

static int sum(struct node *list) {
    int total = 0;
    for (struct node *at = list; at != nullptr; at = at->next)
        total += at->value;
    return total;
}

int main(void) {
    struct node c = {.value = 3};
    struct node b = {.value = 2, .next = &c};
    struct node a = {.value = 1, .next = &b, .some = &c};
    struct node *none;
    struct node *missing = NULL;
    none = a.some;
    int zero = 0;
    printf("%d %d %d %d\n", sum(&a), missing == 0, none->value, a.some->value + zero);
    return 0;
}

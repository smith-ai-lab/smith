// False claims about loops: the prover unrolls a loop as many times as a requires or a constant bounds it, so a loop
// that really runs longer than what it unrolled must be refused or refuted, never proved from the first passes.
// unsound: doubled_count ensures failed
// unsound: bound_on_a_field ensures failed
// unsound: step_over_the_bound ensures failed
// unsound: do_while_runs_once_more ensures failed
// unsound: invariant_after_the_bound invariant failed
#include <smith/contract.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The requires bounds count, the loop runs twice as long.
uint32_t doubled_count(size_t count) {
    smith_requires(count <= 5);
    uint32_t total = 0;
    for (size_t i = 0; i < count * 2; i++)
        total += 1;
    smith_ensures(total <= 5);
    return total;
}

typedef struct {
    size_t count;
} Span;

// The bound is on a field the unroller does not see; the claim assumes 5 passes at most.
uint32_t bound_on_a_field(const Span *span) {
    smith_requires(span->count <= 5);
    uint32_t total = 0;
    for (size_t i = 0; i < span->count; i++)
        total += 1;
    smith_ensures(total <= 4);
    return total;
}

uint32_t step_over_the_bound(size_t count) {
    smith_requires(count <= 5);
    uint32_t total = 0;
    for (size_t i = 0; i < count + 3; i++)
        total += 1;
    smith_ensures(total <= count);
    return total;
}

uint32_t do_while_runs_once_more(size_t count) {
    smith_requires(count <= 4);
    uint32_t total = 0;
    size_t i = 0;
    do {
        total += 1;
        i++;
    } while (i <= count);
    smith_ensures(total <= count);
    return total;
}

// An invariant that holds on the passes the bound covers and breaks on the next.
uint32_t invariant_after_the_bound(size_t count) {
    smith_requires(count <= 3);
    uint32_t total = 0;
    for (size_t i = 0; i < count + 1; i++) {
        smith_invariant(total < 3);
        total += 1;
    }
    return total;
}

int main(int argc, char **argv) {
    if (argc < 2) return 0;
    const char *name = argv[1];
    Span span = {5};
    if (strcmp(name, "doubled_count") == 0) (void)doubled_count(5);
    if (strcmp(name, "bound_on_a_field") == 0) (void)bound_on_a_field(&span);
    if (strcmp(name, "step_over_the_bound") == 0) (void)step_over_the_bound(0);
    if (strcmp(name, "do_while_runs_once_more") == 0) (void)do_while_runs_once_more(0);
    if (strcmp(name, "invariant_after_the_bound") == 0) (void)invariant_after_the_bound(3);
    return 0;
}

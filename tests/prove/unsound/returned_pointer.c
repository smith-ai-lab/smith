// False claims about a returned pointer (issue #74): a pointer is a whole object or null only where the function that
// returns it was proved to, and a caller may load through it only if that function says it is not null.
// unsound: maybe_null ensures failed
// unsound: load_maybe_null null pointer
// unsound: past_the_end buffer-overflow
#include <smith/contract.h>

#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t first;
    uint32_t count;
} Kind;

static Kind kinds[4] = {{0, 3}, {3, 5}, {8, 1}, {9, 7}};

// Claims a result that is not there for id 7.
Kind *maybe_null(uint32_t id) {
    smith_requires(id < 16);
    Kind *found = id < 4 ? &kinds[id] : nullptr;
    smith_ensures(found != nullptr);
    return found;
}

// Says nothing of null, so a caller may not load through the result.
static Kind *find(uint32_t id) {
    smith_requires(id < 16);
    smith_ensures(true);
    return id < 4 ? &kinds[id] : nullptr;
}

uint32_t load_maybe_null(uint32_t id) {
    smith_requires(id < 16);
    Kind *kind = find(id);
    smith_ensures(true);
    return kind->first;
}

// One past the end of the array is not an object.
Kind *past_the_end(uint32_t id) {
    smith_requires(id < 16);
    smith_ensures(true);
    return &kinds[4];
}

// Proved by the contract of past_the_end, which is the claim that fails.
uint32_t load_past_the_end(uint32_t id) {
    smith_requires(id < 16);
    Kind *kind = past_the_end(id);
    smith_ensures(true);
    return kind->first;
}

int main(int argc, char **argv) {
    if (argc < 2) return 0;
    const char *name = argv[1];
    if (strcmp(name, "maybe_null") == 0) (void)maybe_null(7);
    if (strcmp(name, "load_maybe_null") == 0) (void)load_maybe_null(7);
    if (strcmp(name, "past_the_end") == 0) (void)load_past_the_end(0);
    if (strcmp(name, "load_past_the_end") == 0) (void)load_past_the_end(0);
    return 0;
}

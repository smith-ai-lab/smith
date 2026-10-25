#include <smith/test.h>
#include <stdint.h>

typedef struct {
    uint64_t count;
    uint64_t capacity;
} Moves;

void test_exc_member(void) {
    Moves moves = {7, 9};
    uint64_t expected = 8;
    smith_check(moves.count == expected);
}

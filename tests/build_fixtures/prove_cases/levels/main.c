#include <smith/contract.h>
#include <stdint.h>

void hot_begin(uint32_t *counter);

uint32_t twice(uint32_t start) {
    smith_requires(start < 50);
    uint32_t value = start;
    hot_begin(&value);
    hot_begin(&value);
    smith_ensures(value == start + 2);
    return value;
}

int main(void) { return (int)twice(1) - 3; }

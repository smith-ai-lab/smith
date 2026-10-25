#include <smith/contract.h>

#include <stddef.h>
#include <stdint.h>

static int32_t cells[8];

int32_t read_cell(size_t index) {
    smith_requires(index < 8);
    return cells[index];
}

void write_cell(size_t index, int32_t value) {
    smith_requires(index < 8);
    cells[index] = value;
}

int32_t read_past_the_end(void) {
    size_t index = 8;
    return cells[index];
}

// How the cost of a proof grows with the loads in it: a row of 8, and the whole board of 64.
#include <smith/contract.h>

#include <stdint.h>

typedef struct {
    uint8_t cells[8][8];
} Board;

uint32_t row_count(const Board *board, uint8_t row) {
    smith_requires(row < 8);
    uint32_t count = 0;
    for (uint8_t col = 0; col < 8; col++) {
        if (board->cells[row][col] != 0) count++;
    }
    smith_ensures(count <= 8);
    return count;
}

uint32_t board_count(const Board *board) {
    uint32_t count = 0;
    for (uint8_t row = 0; row < 8; row++) {
        for (uint8_t col = 0; col < 8; col++) {
            if (board->cells[row][col] != 0) count++;
        }
    }
    smith_ensures(count <= 64);
    return count;
}

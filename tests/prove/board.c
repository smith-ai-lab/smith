// The off-by-one of checkers' get_user_move: a column check that lets 8 through reads one past a row of 8.
#include <smith/contract.h>

#include <stdint.h>

typedef struct {
    uint8_t cells[8][8];
} Board;

uint8_t cell(const Board *board, uint8_t row, uint8_t col) {
    smith_requires(row < 8 && col < 8);
    return board->cells[row][col];
}

uint8_t cell_wrong(const Board *board, uint8_t row, uint8_t col) {
    smith_requires(row < 8 && col <= 8);
    return board->cells[row][col];
}

// The same loop, with a bound the compiler can see.
uint32_t occupied(const Board *board) {
    uint32_t count = 0;
    for (uint8_t row = 0; row < 8; row++) {
        for (uint8_t col = 0; col < 8; col++) {
            smith_invariant(count <= (uint32_t)row * 8 + col);
            if (board->cells[row][col] != 0) count++;
        }
    }
    smith_ensures(count <= 64);
    return count;
}

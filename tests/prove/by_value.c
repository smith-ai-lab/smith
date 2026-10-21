// A struct passed by value is a well-formed input: its bytes are free, never poison (issue #41).
#include <smith/contract.h>
#include <stdint.h>

typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t dest_row;
    uint8_t dest_col;
} Move;

typedef struct {
    uint8_t cells[8][8];
} Board;

// Branching on a field of the argument; with the argument taken as poison this was reported as undefined behaviour.
uint8_t apply_move(Board *board, Move move) {
    smith_requires(move.row < 8 && move.col < 8 && move.dest_row < 8 && move.dest_col < 8);
    uint8_t moved = 0;
    if (move.row != move.dest_row) {
        board->cells[move.dest_row][move.dest_col] = board->cells[move.row][move.col];
        moved = 1;
    }
    smith_ensures(moved <= 1);
    return moved;
}

// The fields are free: a wrong promise about them is refuted by a value.
uint8_t same_row(Move move) {
    smith_ensures(move.row == move.dest_row);
    return move.row == move.dest_row;
}

// A mixed struct (padding between the fields).
typedef struct {
    uint8_t flag;
    uint32_t count;
} Padded;

uint32_t count_of(Padded padded) {
    const uint32_t count = padded.count;
    smith_ensures(count == padded.count);
    return count;
}

uint32_t bump_count(Padded padded) {
    smith_requires(padded.count < 100);
    uint32_t count = padded.count + 1;
    smith_ensures(count > padded.count);
    return count;
}

// A branch on a field, with no requires: the first thing the prover reported as undefined behaviour.
uint8_t is_step(Move move) {
    uint8_t step = 0;
    if (move.dest_row == move.row + 1) step = 1;
    smith_ensures(step <= 1);
    return step;
}

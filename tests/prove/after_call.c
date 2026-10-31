// A caller reads what a callee with a contract wrote, after the call and after another: the callee indexes arrays and
// calls helpers, so it has UBSan's checks, and the caller's loads must not be undefined behaviour (issue #62: LLVM does
// not infer nofree for a function that calls a trap, and Alive2 then took the call as possibly freeing the caller's
// objects).
#include <smith/contract.h>
#include <stdint.h>

typedef struct {
    uint8_t buf[16];
    uint8_t len;
} List;

typedef struct {
    List steps;
    List jumps;
} Lists;

typedef struct {
    int32_t cells[8][8];
    int32_t turn;
} Board;

static void add_step(Lists *restrict lists, const Board *board, uint8_t row, uint8_t col) {
    smith_requires(row < 8 && col < 8);
    smith_requires(lists->steps.len + 2 <= 16);
    if (row < 7 && board->cells[row + 1][col] == 0) lists->steps.buf[lists->steps.len++] = row;
    if (row < 7 && col > 0 && board->cells[row + 1][col - 1] == 0) lists->steps.buf[lists->steps.len++] = row;
}

static void add_jump(Lists *restrict lists, const Board *board, uint8_t row, uint8_t col) {
    smith_requires(row < 8 && col < 8);
    smith_requires(lists->jumps.len + 2 <= 16);
    if (row < 6 && board->cells[row + 2][col] == 0) lists->jumps.buf[lists->jumps.len++] = row;
    if (row < 6 && col > 1 && board->cells[row + 2][col - 2] == 0) lists->jumps.buf[lists->jumps.len++] = row;
}

static void add_square(const Board *board, Lists *restrict lists, uint8_t row, uint8_t col) {
    smith_requires(board->turn == 0 || board->turn == 1);
    smith_requires(row < 8 && col < 8);
    smith_requires(lists->steps.len <= 2 * col && lists->jumps.len <= 2 * col);
    if (board->cells[row][col] == board->turn + 1) {
        add_jump(lists, board, row, col);
        add_step(lists, board, row, col);
    }
    smith_ensures(lists->steps.len <= 2 * (col + 1) && lists->jumps.len <= 2 * (col + 1));
}

void add_row(const Board *board, Lists *restrict lists, uint8_t row) {
    smith_requires(board->turn == 0 || board->turn == 1);
    smith_requires(row < 8);
    smith_requires(lists->steps.len == 0 && lists->jumps.len == 0);
    for (uint8_t col = 0; col < 2; col++) {
        smith_invariant(lists->steps.len <= 2 * col && lists->jumps.len <= 2 * col);
        add_square(board, lists, row, col);
    }
    smith_ensures(lists->steps.len <= 4 && lists->jumps.len <= 4);
}

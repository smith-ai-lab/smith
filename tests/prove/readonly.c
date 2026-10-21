// A callee that takes a const pointer does not change what it points to, and a callee with many branches and checks
// (UBSan's traps, a loop in its contract) can still be used through its contract (issue #41).
#include <smith/contract.h>
#include <stdint.h>

typedef struct {
    uint8_t buf[16];
    uint8_t len;
} List;

typedef struct {
    int32_t cells[8][8];
    int32_t turn;
} Board;

// Appends the row of a cell when the cell is 1; reads the board, writes the list.
static void add_if_set(List *list, const Board *board, uint8_t row, uint8_t col) {
    smith_requires(row < 8 && col < 8);
    smith_requires(list->len + 1 <= 16);
    const uint8_t old = list->len;
    if (board->cells[row][col] == 1) list->buf[list->len++] = row;
    smith_ensures(list->len == old || list->len == old + 1);
}

// The board is the same after the call: its turn is what it was, though the callee wrote through its other pointer.
int32_t turn_after(List *list, const Board *board, uint8_t row, uint8_t col) {
    smith_requires(row < 8 && col < 8);
    smith_requires(list->len + 1 <= 16);
    const int32_t turn = board->turn;
    add_if_set(list, board, row, col);
    const int32_t after = board->turn;
    smith_ensures(after == turn);
    return after;
}

// A wrong claim about the list is refuted by a value.
uint8_t grows_by_one(List *list, const Board *board, uint8_t row, uint8_t col) {
    smith_requires(row < 8 && col < 8);
    smith_requires(list->len + 1 <= 16);
    const uint8_t old = list->len;
    add_if_set(list, board, row, col);
    const uint8_t now = list->len;
    smith_ensures(now == old + 1);
    return now;
}

// Many blocks (far more than LLVM's reachability analysis is willing to search) between the requires and the ensures.
static void spread(List *list, uint8_t x) {
    smith_requires(list->len + 4 <= 16);
    const uint8_t old = list->len;
    for (uint8_t i = 0; i < 4; i++) {
        if (x & 1) list->buf[list->len++] = 1;
        x = (uint8_t)(x >> 1);
        if (x & 1) list->buf[list->len] = 2;
        x = (uint8_t)(x >> 1);
        if (x & 1) list->buf[list->len] = 3;
        x = (uint8_t)(x >> 1);
    }
    smith_ensures(list->len >= old && list->len <= old + 4);
}

uint8_t spread_then_check(List *list, uint8_t x) {
    smith_requires(list->len + 4 <= 16);
    const uint8_t old = list->len;
    spread(list, x);
    const uint8_t now = list->len;
    smith_ensures(now >= old && now <= old + 4);
    return now;
}

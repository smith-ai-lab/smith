// checkers: the fixed get_valid_diagonals (checkers e8dabe0), with the contracts of issue #8, written with the append as a function with a contract (issue #221).
#include <smith/contract.h>

#include <assert.h>
#include <stdint.h>

constexpr uint8_t rows = 8;
constexpr uint8_t cols = 8;

typedef enum { LIGHT_EMPTY = 0, DARK_EMPTY = 1, DARK_P1 = 2, DARK_P2 = 3 } Cell;
typedef enum { P1, P2 } Turn;
typedef struct {
  Cell cells[rows][cols];
  Turn turn;
} Board;
typedef struct {
  uint8_t row;
  uint8_t col;
} Pos;
typedef struct {
  Pos from;
  Pos to;
} Move;
constexpr uint8_t max_moves = 100;
typedef struct {
  Move buf[max_moves];
  uint8_t len;
} MoveList;
typedef struct {
  MoveList diagonals;
  MoveList jumps;
} Moves;

// ---- the specification: ordinary C, with loops of constant bounds ----

static int forward(const Board *board) { return board->turn == P1 ? 1 : -1; }

// A step of the player to move, from (row, col) one row forward and one column aside, to a free dark square.
static bool step_is_legal(const Board *board, uint8_t row, uint8_t col, int column_step) {
  int to_row = row + forward(board);
  int to_col = col + column_step;
  return 0 <= to_row && to_row < rows && 0 <= to_col && to_col < cols && board->cells[to_row][to_col] == DARK_EMPTY;
}

static bool is_that_step(Move move, const Board *board, uint8_t row, uint8_t col, int column_step) {
  return move.from.row == row && move.from.col == col && move.to.row == row + forward(board) &&
         move.to.col == col + column_step;
}

// Everything added after `old` is a legal step from (row, col); plus_legal and minus_legal say, as step_is_legal does,
// which of the two steps are legal. At most two are added, so the loop has a bound of two.
static bool added_only_legal_steps(const Board *board, const MoveList *list, uint8_t old, uint8_t row, uint8_t col,
                                   bool plus_legal, bool minus_legal) {
  for (uint8_t k = 0; k < 2; k++) {
    uint8_t i = old + k;
    if (i >= list->len) break;
    Move move = list->buf[i];
    bool legal = (is_that_step(move, board, row, col, 1) && plus_legal) ||
                 (is_that_step(move, board, row, col, -1) && minus_legal);
    if (!legal) return false;
  }
  return true;
}

static bool added_that_step(const MoveList *list, uint8_t old, const Board *board, uint8_t row, uint8_t col, int column_step) {
  for (uint8_t k = 0; k < 2; k++) {
    uint8_t i = old + k;
    if (i < list->len && is_that_step(list->buf[i], board, row, col, column_step)) return true;
  }
  return false;
}

// Every legal step from (row, col) was added after `old`.
static bool added_every_legal_step(const Board *board, const MoveList *list, uint8_t old, uint8_t row, uint8_t col,
                                   bool plus_legal, bool minus_legal) {
  return (!plus_legal || added_that_step(list, old, board, row, col, 1)) &&
         (!minus_legal || added_that_step(list, old, board, row, col, -1));
}

// The append the move generator does twice: the move goes at the end of the list. It has a contract, so that the proof
// of get_valid_diagonals uses what it promises (the length grew by one, the move is at the old end, nothing else was
// written) and not its stores to a symbolic place of a 100-move buffer, which every query of the proof paid for.
static void push_move(MoveList *list, Move move) {
  smith_requires(list->len < max_moves);
  smith_modifies(&list->len, sizeof list->len);
  smith_modifies(&list->buf[list->len], sizeof list->buf[0]);
  const uint8_t end = list->len;
  list->buf[end] = move;
  list->len = end + 1;
  smith_ensures(list->len == end + 1);
  smith_ensures(list->buf[end].from.row == move.from.row && list->buf[end].from.col == move.from.col &&
                list->buf[end].to.row == move.to.row && list->buf[end].to.col == move.to.col);
}

void get_valid_diagonals(Moves *restrict moves, const Board *restrict board, uint8_t row, uint8_t col) {
  smith_requires(row < rows && col < cols);
  smith_requires(moves->diagonals.len + 2 <= max_moves);
  smith_requires(board->turn == P1 || board->turn == P2);
  const uint8_t old = moves->diagonals.len;
  // The board is read once for each step, for the specification and for the move.
  const bool plus_legal = step_is_legal(board, row, col, 1);
  const bool minus_legal = step_is_legal(board, row, col, -1);
  if (plus_legal) {
    push_move(&moves->diagonals, (Move){ .from = { .row = row, .col = col },
                                         .to = { .row = row + forward(board), .col = col + 1 } });
  }
  if (minus_legal) {
    push_move(&moves->diagonals, (Move){ .from = { .row = row, .col = col },
                                         .to = { .row = row + forward(board), .col = col - 1 } });
  }
  smith_ensures(moves->diagonals.len <= old + 2);
  smith_ensures(added_only_legal_steps(board, &moves->diagonals, old, row, col, plus_legal, minus_legal));
  smith_ensures(added_every_legal_step(board, &moves->diagonals, old, row, col, plus_legal, minus_legal));
}

// checkers: get_valid_diagonals before its fixes (checkers 2d6bbfa): the else-if and col > 1 miss legal moves, with the contracts of issue #8.
// The step is chosen by a function with a contract (choose_step) from the two legalities the caller reads once (issue #268).
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

// Everything added after `old` is a legal step from (row, col). At most two are added, so the loop has a bound of two.
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

// The append the move generator does four times: the move goes at the end of the list. It has a contract, so that the
// proof of get_valid_diagonals uses what it promises (the length grew by one, the move is at the old end, nothing else
// was written) and not its stores to a symbolic place of a 100-move buffer, which every query of the proof paid for.
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

// The choice of the step, before its fixes, given which of the two steps are legal: the plus column first, the minus
// column only from column 2 on, and for P2 only from row 2 on. It returns the column step (+1 or -1; 0 for no step) and
// has a contract, and it does not read the board: the caller reads the board once, for the specification and for this
// choice, so the proof of get_valid_diagonals never pays for the 8-by-8 board's symbolic loads twice (issue #268).
static int choose_step(Turn turn, uint8_t row, uint8_t col, bool plus_legal, bool minus_legal) {
  smith_requires(row < rows && col < cols);
  smith_requires(turn == P1 || turn == P2);
  smith_requires(!plus_legal || ((turn == P1 ? row < 7 : row > 0) && col < 7));
  smith_requires(!minus_legal || ((turn == P1 ? row < 7 : row > 0) && col > 0));
  const bool row_ok = turn == P1 || row > 1;
  int column_step = 0;
  if (row_ok) {
    if (plus_legal) {
      column_step = 1;
    } else if (col > 1 && minus_legal) {
      column_step = -1;
    }
  }
  smith_ensures(column_step == (!row_ok ? 0 : plus_legal ? 1 : (col > 1 && minus_legal) ? -1 : 0));
  return column_step;
}

void get_valid_diagonals(Moves *restrict moves, const Board *restrict board, uint8_t row, uint8_t col) {
  smith_requires(row < rows && col < cols);
  smith_requires(moves->diagonals.len + 2 <= max_moves);
  smith_requires(board->turn == P1 || board->turn == P2);
  const uint8_t old = moves->diagonals.len;
  const bool plus_legal = step_is_legal(board, row, col, 1);
  const bool minus_legal = step_is_legal(board, row, col, -1);
  const int column_step = choose_step(board->turn, row, col, plus_legal, minus_legal);
  if (column_step != 0) {
    const Pos to = { .row = row + forward(board), .col = col + column_step };
    push_move(&moves->diagonals, (Move){ .from = { .row = row, .col = col }, .to = to });
  }
  smith_ensures(moves->diagonals.len <= old + 2);
  smith_ensures(added_only_legal_steps(board, &moves->diagonals, old, row, col, plus_legal, minus_legal));
  smith_ensures(added_every_legal_step(board, &moves->diagonals, old, row, col, plus_legal, minus_legal));
}

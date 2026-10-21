// checkers: get_valid_diagonals before its fixes (checkers 2d6bbfa): the else-if and col > 1 miss legal moves, with the contracts of issue #8.
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
static bool added_only_legal_steps(const Board *board, const MoveList *list, uint8_t old, uint8_t row, uint8_t col) {
  for (uint8_t k = 0; k < 2; k++) {
    uint8_t i = old + k;
    if (i >= list->len) break;
    Move move = list->buf[i];
    bool legal = (is_that_step(move, board, row, col, 1) && step_is_legal(board, row, col, 1)) ||
                 (is_that_step(move, board, row, col, -1) && step_is_legal(board, row, col, -1));
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
static bool added_every_legal_step(const Board *board, const MoveList *list, uint8_t old, uint8_t row, uint8_t col) {
  return (!step_is_legal(board, row, col, 1) || added_that_step(list, old, board, row, col, 1)) &&
         (!step_is_legal(board, row, col, -1) || added_that_step(list, old, board, row, col, -1));
}

void get_valid_diagonals(Moves *moves, const Board *board, uint8_t row, uint8_t col) {
  smith_requires(row < rows && col < cols);
  smith_requires(moves->diagonals.len + 2 <= max_moves);
  smith_requires(board->turn == P1 || board->turn == P2);
  const uint8_t old = moves->diagonals.len;
  switch (board->turn) {
    case P1:
      if (row < 7 && col < 7 && board->cells[row+1][col+1] == DARK_EMPTY) {
        assert(moves->diagonals.len < max_moves);
        moves->diagonals.buf[moves->diagonals.len++] = (Move){
          .from = { .row = row, .col = col },
          .to = { .row = row+1, .col = col + 1 }
        };
      } else if (row < 7 && col > 1 && board->cells[row+1][col-1] == DARK_EMPTY) {
        assert(moves->diagonals.len < max_moves);
        moves->diagonals.buf[moves->diagonals.len++] = (Move){
          .from = { .row = row, .col = col },
          .to = { .row = row+1, .col = col - 1 }
        };
      }
      break;
    case P2:
      if (row > 1 && col < 7 && board->cells[row-1][col+1] == DARK_EMPTY) {
        assert(moves->diagonals.len < max_moves);
        moves->diagonals.buf[moves->diagonals.len++] = (Move){
          .from = { .row = row, .col = col },
          .to = { .row = row - 1, .col = col + 1 }
        };
      } else if (row > 1 && col > 1 && board->cells[row-1][col-1] == DARK_EMPTY) {
        assert(moves->diagonals.len < max_moves);
        moves->diagonals.buf[moves->diagonals.len++] = (Move){
          .from = { .row = row, .col = col },
          .to = { .row = row - 1, .col = col - 1 }
        };
      }
      break;
  }
  smith_ensures(moves->diagonals.len <= old + 2);
  smith_ensures(added_only_legal_steps(board, &moves->diagonals, old, row, col));
  smith_ensures(added_every_legal_step(board, &moves->diagonals, old, row, col));
}

#include "grid.h"
#include <stdio.h>
int main(void) {
struct grid_Board board = {};
board.cells[15] = 1;
printf("%zu %zu %zu\n", grid_board_size(), grid_other_sizes(), sizeof board.cells);
return 0;
}

#include "k.h"

struct grid_Plain { __UINT8_TYPE__ cells[SIDE]; };
typedef struct grid_Plain grid_Plain;
typedef struct { __UINT8_TYPE__ cells[SIDE][depth]; __UINT32_TYPE__ n; } grid_Typed;
struct grid_Nested { struct { __UINT8_TYPE__ x[SIDE * 2]; } in; __UINT8_TYPE__ y[SIDE + 1]; };
typedef struct grid_Nested grid_Nested;
static __UINT8_TYPE__ grid_table[SIDE];
struct grid_Board { __UINT8_TYPE__ cells[SIDE * SIDE]; __UINT32_TYPE__ n; };
typedef struct grid_Board grid_Board;
__SIZE_TYPE__ grid_board_size([[maybe_unused]] __INT32_TYPE__ n) __asm__("_grid::board_size");
__SIZE_TYPE__ grid_board_size([[maybe_unused]] __INT32_TYPE__ n) {
return sizeof(struct grid_Board);
}
__SIZE_TYPE__ grid_other_sizes() __asm__("_grid::other_sizes");
__SIZE_TYPE__ grid_other_sizes() {
return sizeof(struct grid_Plain) + sizeof(grid_Typed) + sizeof(struct grid_Nested) + sizeof grid_table;
}

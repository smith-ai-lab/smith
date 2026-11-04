#pragma once
#include <stddef.h>
#include <stdint.h>
#include "k.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint8_t cells[SIDE * SIDE]; uint32_t n; } grid_Board;

size_t grid_board_size(void) __asm__("grid::board_size");

size_t grid_other_sizes(void) __asm__("grid::other_sizes");
#ifdef __cplusplus
}
#endif

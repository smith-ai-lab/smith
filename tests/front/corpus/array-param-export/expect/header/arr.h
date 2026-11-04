#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int32_t arr_pick(int32_t xs[4], size_t i) __asm__("arr::pick");

int32_t arr_corner(int32_t m[3][2]) __asm__("arr::corner");

int32_t arr_pick_last(int32_t xs[4]) __asm__("arr::pick_last");
#ifdef __cplusplus
}
#endif

#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t lib_cost(uint32_t n) __asm__("lib::cost");

uint32_t lib_twice(uint32_t n) __asm__("lib::twice");
#ifdef __cplusplus
}
#endif

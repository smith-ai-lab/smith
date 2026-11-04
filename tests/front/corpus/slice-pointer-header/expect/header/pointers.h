#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#ifndef SMITH_ARRAY_char_n
#define SMITH_ARRAY_char_n
struct smith_array_char_n { char * data; size_t count [1]; };
#endif

#ifndef SMITH_ARRAY_char_n
#define SMITH_ARRAY_char_n
struct smith_array_char_n { char * data; size_t count [1]; };
#endif
bool pointers_take_line(struct smith_array_char_n *text, struct smith_array_char_n *line) __asm__("pointers::take_line");
#ifdef __cplusplus
}
#endif

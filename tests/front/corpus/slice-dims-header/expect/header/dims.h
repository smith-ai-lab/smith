#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#ifndef SMITH_ARRAY_f32_n_n
#define SMITH_ARRAY_f32_n_n
struct smith_array_f32_n_n { float * data; size_t count [2]; };
#endif

#ifndef SMITH_ARRAY_u8_n_3
#define SMITH_ARRAY_u8_n_3
struct smith_array_u8_n_3 { uint8_t * data; size_t count [1]; };
#endif
typedef struct { struct smith_array_f32_n_n pixels; struct smith_array_u8_n_3 rows; } dims_Image;
#ifdef __cplusplus
}
#endif

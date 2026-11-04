#pragma once
#include <stddef.h>
#include <stdint.h>
#include "core.h"
#ifdef __cplusplus
extern "C" {
#endif
int32_t rolls_total(const core_Thing *thing, int32_t more) __asm__("rolls::total");
#ifdef __cplusplus
}
#endif

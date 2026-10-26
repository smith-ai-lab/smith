// The first primes, in their own file so that main.c and the tests both call them.
#pragma once

#include <stddef.h>
#include <stdint.h>

// The sum of the first `count` primes, which is at most five.
int32_t primes_sum(size_t count);

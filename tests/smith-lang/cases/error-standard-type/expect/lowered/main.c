#include <stddef.h>
#include <stdint.h>

__UINT64_TYPE__ widen(uint32_t x, size_t n, ptrdiff_t d) {
return x + n + d;
}

#include <stddef.h>
#include <stdio.h>
int main() {
printf("%zu %zu %zu %zu\n", sizeof(__UINT8_TYPE__), sizeof(__UINT16_TYPE__), sizeof(__UINT32_TYPE__), sizeof(__UINT64_TYPE__));
printf("%zu %zu %zu %zu\n", sizeof(__INT8_TYPE__), sizeof(__INT16_TYPE__), sizeof(__INT32_TYPE__), sizeof(__INT64_TYPE__));
printf("%zu %zu\n", sizeof(__SIZE_TYPE__), sizeof(__PTRDIFF_TYPE__));
printf("%zu %zu %zu\n", sizeof(_Float16), sizeof(float), sizeof(double));
__INT8_TYPE__ minus = -1;
__UINT8_TYPE__ wrap = (__UINT8_TYPE__)minus;
printf("%d %u\n", minus, wrap);
float half = 0.5f;
double wide = half;
printf("%.2f %.2f\n", (double)half, wide);
static_assert(_Generic((__UINT64_TYPE__)0, unsigned long long: 1, unsigned long: 1, default: 0), "u64 is an unsigned type");
static_assert(_Generic((__SIZE_TYPE__)0, size_t: 1, default: 0), "usize is size_t");
static_assert(_Generic((__PTRDIFF_TYPE__)0, ptrdiff_t: 1, default: 0), "isize is ptrdiff_t");
Bool flag = true;
printf("%d\n", flag);
return 0;
}

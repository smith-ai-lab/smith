#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static_assert(sizeof(__SIZE_TYPE__) == sizeof(__UINT64_TYPE__));
int main() {
__SIZE_TYPE__ small = 3;
assert(small < SIZE_MAX);
assert(UINT64_MAX > 0 && small != SIZE_MAX);
printf("%zu\n", small);
return 0;
}

#include "pick.h"
#include <stdio.h>
typedef struct {
__UINT32_TYPE__ tag;
union {
float real;
__UINT32_TYPE__ bits;
};
} Cell;
typedef enum : U8 { Small = 3, Large = 200 } Size;
static __INT32_TYPE__ triple(__INT32_TYPE__ v) {
return v * 3;
}
int main() {
__UINT8_TYPE__ block[16] = {0, 1, 2};
void *bytes = block;
__SIZE_TYPE__ size = 16;
Cell cell = {.tag = 9, .real = 1.5f};
Cell *pointer = &cell;
__INT32_TYPE__ n = 4;
printf("%d\n",smith_call_failed);
printf("%g %u\n",smith_call_failed,smith_call_failed);
printf("%u %u\n",smith_call_failed,smith_call_failed);
printf("%d %d\n",smith_call_failed, n);
printf("%d\n",smith_call_failed);
printf("%d\n",smith_call_failed);
printf("%zu %zu\n",smith_call_failed,smith_call_failed);
return 0;
}

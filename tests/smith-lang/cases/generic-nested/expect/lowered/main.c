#include "arena.h"
#include "list.h"
#include <stdio.h>

typedef struct {
__INT32_TYPE__ x;
__INT32_TYPE__ y;
} main_Point;
int main(void) {
__UINT8_TYPE__ storage[256];
arena_Arena a = arena_make(storage, sizeof storage);
main_Point *p = list_make_two(&a, main_Point);
p->x = 7;
__SIZE_TYPE__ after_point = list_measure(&a, __UINT64_TYPE__);
printf("%d %zu %zu\n", p->x, after_point, a.used);
return 0;
}

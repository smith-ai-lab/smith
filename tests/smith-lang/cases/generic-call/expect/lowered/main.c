#include "arena.h"
#include <stdio.h>
typedef struct {
__INT32_TYPE__ x;
__INT32_TYPE__ y;
} Point;
int main(void) {
__UINT8_TYPE__ storage[256];
arena_Arena a = arena_make(storage, sizeof storage);
Point *p = arena_push(&a, Point);
p->x = 3;
p->y = 4;
__UINT64_TYPE__ *n = arena_push(&a, __UINT64_TYPE__);
*n = 5;
Point *q = arena_push(&a, Point);
q->x = p->x + 10;
printf("%d %d %llu %d used %zu\n", p->x, p->y, (unsigned long long)*n, q->x, a.used);
return 0;
}

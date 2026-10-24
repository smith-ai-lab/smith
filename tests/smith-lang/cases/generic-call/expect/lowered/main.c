#include "arena.h"
#include <stdio.h>
typedef struct {
__INT32_TYPE__ x;
__INT32_TYPE__ y;
} Point;
int main() {
__UINT8_TYPE__ storage[256];
arena_Arena a = arena_make(storage, sizeof storage);
Point *p =__extension__({extern Point*arena_push__Point(arena_Arena*a)__asm__("_arena::push(Point)");arena_push__Point(&a);});
p->x = 3;
p->y = 4;
__UINT64_TYPE__ *n =__extension__({extern __UINT64_TYPE__*arena_push__u64(arena_Arena*a)__asm__("_arena::push(__UINT64_TYPE__)");arena_push__u64(&a);});
*n = 5;
Point *q =__extension__({extern Point*arena_push__Point(arena_Arena*a)__asm__("_arena::push(Point)");arena_push__Point(&a);});
q->x = p->x + 10;
printf("%d %d %llu %d used %zu\n", p->x, p->y, (unsigned long long)*n, q->x, a.used);
return 0;
}
extern Point*arena_push__Point(arena_Arena*a)__asm__("_arena::push(Point)");
[[gnu::weak]][[gnu::visibility("hidden")]]Point*arena_push__Point(arena_Arena*a){__SIZE_TYPE__ aligned=(a->used+alignof(Point)-1)&~(alignof(Point)-1);Point*result=(Point*)(a->base+aligned);a->used=aligned+sizeof(Point);return result;}
extern __UINT64_TYPE__*arena_push__u64(arena_Arena*a)__asm__("_arena::push(__UINT64_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__UINT64_TYPE__*arena_push__u64(arena_Arena*a){__SIZE_TYPE__ aligned=(a->used+alignof(__UINT64_TYPE__)-1)&~(alignof(__UINT64_TYPE__)-1);__UINT64_TYPE__*result=(__UINT64_TYPE__*)(a->base+aligned);a->used=aligned+sizeof(__UINT64_TYPE__);return result;}

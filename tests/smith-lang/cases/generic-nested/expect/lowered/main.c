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
main_Point *p =__extension__({extern main_Point*list_make_two__Point(arena_Arena*a)__asm__("_list::make_two(main_Point)");list_make_two__Point(&a);});
p->x = 7;
__SIZE_TYPE__ after_point =__extension__({extern __SIZE_TYPE__ list_measure__U64(arena_Arena*a)__asm__("_list::measure(__UINT64_TYPE__)");list_measure__U64(&a);});
printf("%d %zu %zu\n", p->x, after_point, a.used);
return 0;
}
extern main_Point*list_make_two__Point(arena_Arena*a)__asm__("_list::make_two(main_Point)");
[[gnu::weak]][[gnu::visibility("hidden")]]main_Point*list_make_two__Point(arena_Arena*a){__extension__({extern main_Point*list_make__Point(arena_Arena*a)__asm__("_list::make(main_Point)");list_make__Point(a);});return __extension__({extern main_Point*list_make__Point(arena_Arena*a)__asm__("_list::make(main_Point)");list_make__Point(a);});}
extern __SIZE_TYPE__ list_measure__U64(arena_Arena*a)__asm__("_list::measure(__UINT64_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ list_measure__U64(arena_Arena*a){__UINT64_TYPE__*value=__extension__({extern __UINT64_TYPE__*list_make__U64(arena_Arena*a)__asm__("_list::make(__UINT64_TYPE__)");list_make__U64(a);});(void)value;return list_bytes(a);}
extern main_Point*list_make__Point(arena_Arena*a)__asm__("_list::make(main_Point)");
[[gnu::weak]][[gnu::visibility("hidden")]]main_Point*list_make__Point(arena_Arena*a){return __extension__({extern main_Point*arena_push__Point(arena_Arena*a)__asm__("_arena::push(main_Point)");arena_push__Point(a);});}
extern __UINT64_TYPE__*list_make__U64(arena_Arena*a)__asm__("_list::make(__UINT64_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__UINT64_TYPE__*list_make__U64(arena_Arena*a){return __extension__({extern __UINT64_TYPE__*arena_push__U64(arena_Arena*a)__asm__("_arena::push(__UINT64_TYPE__)");arena_push__U64(a);});}
extern main_Point*arena_push__Point(arena_Arena*a)__asm__("_arena::push(main_Point)");
[[gnu::weak]][[gnu::visibility("hidden")]]main_Point*arena_push__Point(arena_Arena*a){__SIZE_TYPE__ aligned=(a->used+alignof(main_Point)-1)&~(alignof(main_Point)-1);main_Point*result=(main_Point*)(a->base+aligned);a->used=aligned+sizeof(main_Point);return result;}
extern __UINT64_TYPE__*arena_push__U64(arena_Arena*a)__asm__("_arena::push(__UINT64_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__UINT64_TYPE__*arena_push__U64(arena_Arena*a){__SIZE_TYPE__ aligned=(a->used+alignof(__UINT64_TYPE__)-1)&~(alignof(__UINT64_TYPE__)-1);__UINT64_TYPE__*result=(__UINT64_TYPE__*)(a->base+aligned);a->used=aligned+sizeof(__UINT64_TYPE__);return result;}

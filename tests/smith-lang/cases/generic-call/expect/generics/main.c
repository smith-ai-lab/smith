__extension__({ extern Point * arena_push__Point(arena_Arena * a) __asm__("_arena::push(Point)"); arena_push__Point(& a); })
__extension__({ extern __UINT64_TYPE__ * arena_push__u64(arena_Arena * a) __asm__("_arena::push(unsigned long long)"); arena_push__u64(& a); })
__extension__({ extern Point * arena_push__Point(arena_Arena * a) __asm__("_arena::push(Point)"); arena_push__Point(& a); })
extern Point * arena_push__Point(arena_Arena * a) __asm__("_arena::push(Point)");
[[gnu::weak]][[gnu::visibility("hidden")]] Point * arena_push__Point(arena_Arena * a) {
    __SIZE_TYPE__ aligned = (a->used + alignof(Point) - 1) & ~(alignof(Point) - 1);
    Point * result = (Point *)(a->base + aligned);
    a->used = aligned + sizeof(Point);
    return result;
}
extern __UINT64_TYPE__ * arena_push__u64(arena_Arena * a) __asm__("_arena::push(unsigned long long)");
[[gnu::weak]][[gnu::visibility("hidden")]] __UINT64_TYPE__ * arena_push__u64(arena_Arena * a) {
    __SIZE_TYPE__ aligned = (a->used + alignof(__UINT64_TYPE__) - 1) & ~(alignof(__UINT64_TYPE__) - 1);
    __UINT64_TYPE__ * result = (__UINT64_TYPE__ *)(a->base + aligned);
    a->used = aligned + sizeof(__UINT64_TYPE__);
    return result;
}

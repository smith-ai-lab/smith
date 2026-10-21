typedef struct {
__UINT64_TYPE__ used;
__UINT64_TYPE__ capacity;
} SmithArena;
SmithArena smith_arena_create(__UINT64_TYPE__ capacity);
SmithArena smith_arena_create(__UINT64_TYPE__ capacity) {
return (SmithArena){.used = 0, .capacity = capacity};
}
__UINT64_TYPE__ smith_arena_take(SmithArena *arena, __UINT64_TYPE__ size) __asm__("_smith::arena::take");
__UINT64_TYPE__ smith_arena_take(SmithArena *arena, __UINT64_TYPE__ size) {
if (arena->used + size > arena->capacity) return 0;
arena->used += size;
return arena->used;
}
__UINT64_TYPE__ smith_arena_limit = 4096;
__UINT64_TYPE__ smith_arena_checksum(const __UINT8_TYPE__ *data, __SIZE_TYPE__ size);

typedef struct {
__UINT8_TYPE__ *base;
__SIZE_TYPE__ used;
__SIZE_TYPE__ capacity;
} arena_Arena;
arena_Arena arena_make(__UINT8_TYPE__ *memory, __SIZE_TYPE__ capacity) __asm__("_arena::make");
arena_Arena arena_make(__UINT8_TYPE__ *memory, __SIZE_TYPE__ capacity) {
return (arena_Arena){memory, 0, capacity};
} [ [ clang :: unavailable ( "arena::push is a generic function: call it from a .smith module" ) ] ] void arena_push ( void ) ;

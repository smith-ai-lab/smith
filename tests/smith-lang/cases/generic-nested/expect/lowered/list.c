[ [ clang :: unavailable ( "list::make is a generic function: call it from a .smith module" ) ] ] void list_make ( void ) ; [ [ clang :: unavailable ( "list::make_two is a generic function: call it from a .smith module" ) ] ] void list_make_two ( void ) ;
__SIZE_TYPE__ list_bytes(arena_Arena *a) __asm__("_list::bytes");
__SIZE_TYPE__ list_bytes(arena_Arena *a) {
return a->used;
} [ [ clang :: unavailable ( "list::measure is a generic function: call it from a .smith module" ) ] ] void list_measure ( void ) ;

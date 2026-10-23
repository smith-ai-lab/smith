typedef struct {
__INT32_TYPE__ x;
__INT32_TYPE__ y;
} num_Pair; [ [ clang :: unavailable ( "num::add is a generic function: call it from a .smith module" ) ] ] void num_add ( void ) ; [ [ clang :: unavailable ( "num::kind is a generic function: call it from a .smith module" ) ] ] void num_kind ( void ) ; [ [ clang :: unavailable ( "num::mix is a generic function: call it from a .smith module" ) ] ] void num_mix ( void ) ; [ [ clang :: unavailable ( "num::make is a generic function: call it from a .smith module" ) ] ] void num_make ( void ) ;
__INT32_TYPE__ num_first(const num_Pair *p, typeof(p) other) __asm__("_num::first");
__INT32_TYPE__ num_first(const num_Pair *p, typeof(p) other) {
return p != nullptr ? p->x : other->y;
} [ [ clang :: unavailable ( "num::width is a generic function: call it from a .smith module" ) ] ] void num_width ( void ) ;

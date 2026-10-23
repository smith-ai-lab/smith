[ [ clang :: unavailable ( "apply::twice is a generic function: call it from a .smith module" ) ] ] void apply_twice ( void ) ; [ [ clang :: unavailable ( "apply::address_matches is a generic function: call it from a .smith module" ) ] ] void apply_address_matches ( void ) ;
__INT32_TYPE__ apply_triple(__INT32_TYPE__ x) __asm__("_apply::triple");
__INT32_TYPE__ apply_triple(__INT32_TYPE__ x) {
return x * 3;
} [ [ clang :: unavailable ( "apply::again is a generic function: call it from a .smith module" ) ] ] void apply_again ( void ) ; [ [ clang :: unavailable ( "apply::call_with_pair is a generic function: call it from a .smith module" ) ] ] void apply_call_with_pair ( void ) ;

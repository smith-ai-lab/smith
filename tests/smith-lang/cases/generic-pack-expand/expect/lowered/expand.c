static __INT32_TYPE__ expand_sq(__INT32_TYPE__ x) {
return x * x;
}
static __INT32_TYPE__ expand_plus(__INT32_TYPE__ a, __INT32_TYPE__ b) {
return a + b;
}
static __INT32_TYPE__ expand_vsum(__INT32_TYPE__ count, ...) {
__builtin_va_list list;
__builtin_va_start(list, count);
__INT32_TYPE__ sum = 0;
for (__INT32_TYPE__ i = 0; i < count; i++) sum = sum * 10 + __builtin_va_arg(list, __INT32_TYPE__);
__builtin_va_end(list);
return sum;
} [ [ clang :: unavailable ( "expand::shifted is a generic function: call it from a .smith module" ) ] ] void expand_shifted ( void ) ; [ [ clang :: unavailable ( "expand::around is a generic function: call it from a .smith module" ) ] ] void expand_around ( void ) ; [ [ clang :: unavailable ( "expand::rest_empty is a generic function: call it from a .smith module" ) ] ] void expand_rest_empty ( void ) ; [ [ clang :: unavailable ( "expand::table is a generic function: call it from a .smith module" ) ] ] void expand_table ( void ) ; [ [ clang :: unavailable ( "expand::first_of is a generic function: call it from a .smith module" ) ] ] void expand_first_of ( void ) ; [ [ clang :: unavailable ( "expand::over_generic is a generic function: call it from a .smith module" ) ] ] void expand_over_generic ( void ) ;
typedef struct {
__INT32_TYPE__ a;
__INT32_TYPE__ b;
} expand_Pair; [ [ clang :: unavailable ( "expand::fields_of is a generic function: call it from a .smith module" ) ] ] void expand_fields_of ( void ) ;

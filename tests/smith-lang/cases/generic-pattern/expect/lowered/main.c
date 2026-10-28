#include <stdio.h>
 [ [ clang :: unavailable ( "main::push is a generic function: call it from a .smith module" ) ] ] void main_push ( void ) ; [ [ clang :: unavailable ( "main::last is a generic function: call it from a .smith module" ) ] ] void main_last ( void ) ;
__INT32_TYPE__ main(void) {
__INT32_TYPE__ ints[4] = {};
double reals[4] = {};struct smith_struct_List_i32 {__INT32_TYPE__ *items;
__SIZE_TYPE__ count; };
struct smith_struct_List_i32 a = {.items = ints};struct smith_struct_List_f64 {double *items;
__SIZE_TYPE__ count; };
struct smith_struct_List_f64 b = {.items = reals};smith_call_failed;smith_call_failed;smith_call_failed;
printf("%zu %d %zu %g\n", a.count,smith_call_failed, b.count,smith_call_failed);
return 0;
}

#include <stdio.h>
 [ [ clang :: unavailable ( "main::head is a generic function: call it from a .smith module" ) ] ] void main_head ( void ) ; [ [ clang :: unavailable ( "main::tail is a generic function: call it from a .smith module" ) ] ] void main_tail ( void ) ; [ [ clang :: unavailable ( "main::sum is a generic function: call it from a .smith module" ) ] ] void main_sum ( void ) ; [ [ clang :: unavailable ( "main::zero is a generic function: call it from a .smith module" ) ] ] void main_zero ( void ) ; [ [ clang :: unavailable ( "main::scale is a generic function: call it from a .smith module" ) ] ] void main_scale ( void ) ;struct smith_struct_Box_i32 {__INT32_TYPE__ value; };
typedef struct smith_struct_Box_i32 main_IntBox;struct smith_struct_Pair_i32__f64 {__INT32_TYPE__ first; double second; };
typedef struct smith_struct_Pair_i32__f64 main_Mixed;
__INT32_TYPE__ main(void) {
main_Mixed m = {.first = 7, .second = 1.5};struct smith_struct_Box_f32 {float value; };
struct smith_struct_Box_f32 real = {.value = 0.5f};
main_IntBox whole = {.value = 40}; struct smith_enum_option_main_IntBox {enum smith_enum_option_main_IntBox_tag {smith_enum_option_main_IntBox_tag_none, smith_enum_option_main_IntBox_tag_some } tag; union { main_IntBox some; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_option_main_IntBox maybe = {.some = whole};
printf("%d %g %g %g %d %g\n",smith_call_failed,smith_call_failed, (double)smith_call_failed, (double)smith_call_failed,smith_call_failed,smith_call_failed);
return 0;
}

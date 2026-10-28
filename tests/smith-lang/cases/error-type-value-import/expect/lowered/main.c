#include "shapes.h"
 [ [ clang :: unavailable ( "main::make is a generic function: call it from a .smith module" ) ] ] void main_make ( void ) ;
int main(void) {struct smith_array_f32_n {
float * data; __SIZE_TYPE__ count [1]; } xs =__extension__({extern int main_make__F32(void)__asm__("_main::make(float)");main_make__F32();});
return 0;
}
extern int main_make__F32(void)__asm__("_main::make(float)");
[[gnu::weak]][[gnu::visibility("hidden")]]int main_make__F32(void){static_assert(0>0,"make needs a size for each [:]");struct smith_array_f32_n result={};return result;}

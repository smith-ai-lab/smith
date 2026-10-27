#include <stdio.h>
#include <stdlib.h>

typedef struct {struct smith_array_f64_n {
double * data; __SIZE_TYPE__ count [1]; } xs;
__INT32_TYPE__ fixed[3];
__INT32_TYPE__ grid[2][2];struct smith_array_f32_n_2 {
float * data; __SIZE_TYPE__ count [1]; } rows;
__INT32_TYPE__ n;
} main_Bag;
typedef struct {
main_Bag bag;
} main_Outer;
void smith_failure(__UINT8_TYPE__ code);
void smith_failure(__UINT8_TYPE__ code) {
exit(code);
}
static double main_total(struct smith_array_f64_n xs) {
double t = 0;
for (__SIZE_TYPE__ i = 0; i < countof(xs); i++)
t += xs[i];
return t;
} [ [ clang :: unavailable ( "main::show is a generic function: call it from a .smith module" ) ] ] void main_show ( void ) ; [ [ clang :: unavailable ( "main::first is a generic function: call it from a .smith module" ) ] ] void main_first ( void ) ;
__INT32_TYPE__ main(void) {
double v[4] = {1, 2, 3, 4};
float r[3][2] = {{1, 2}, {3, 4}, {5, 6}};struct smith_array_f32_n_2 rows = r;
main_Bag a = {.xs = v[0:4], .fixed = {5, 6, 7}, .grid = {{1, 2}, {3, 4}}, .rows = rows, .n = 1};__extension__({extern void main_show__Bag(main_Bag*x,main_Bag y)__asm__("_main::show(main_Bag)");main_show__Bag(&a,a);});
main_Outer o = {a};
printf("first %g\n",__extension__({extern double main_first__Bag(main_Outer*o)__asm__("_main::first(main_Bag)");main_first__Bag(&o);}));
return 0;
}
typedef struct smith_field{struct smith_array_char_n{char*data;__SIZE_TYPE__ count[1];}name;unsigned __int128 type;struct smith_array_char_n doc;}Field;[[maybe_unused]]static inline Field smith_field_make(char*name,__SIZE_TYPE__ length,unsigned __int128 kind,char*doc,__SIZE_TYPE__ doc_length){return(Field){{name,{length}},kind,{doc,{doc_length}}};}extern void main_show__Bag(main_Bag*x,main_Bag y)__asm__("_main::show(main_Bag)");
[[gnu::weak]][[gnu::visibility("hidden")]]void main_show__Bag(main_Bag*x,main_Bag y){}
extern double main_first__Bag(main_Outer*o)__asm__("_main::first(main_Bag)");
[[gnu::weak]][[gnu::visibility("hidden")]]double main_first__Bag(main_Outer*o){constexpr Field f=smith_call_failed;return o->bag.smith_call_failed[0]+o->bag.smith_call_failed[smith_call_failed-1];}

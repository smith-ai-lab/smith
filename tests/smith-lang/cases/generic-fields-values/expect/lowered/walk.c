#include <stdio.h>

typedef struct {
__INT32_TYPE__ id;
float weight;
double scale;
__UINT8_TYPE__ flag;
} walk_Sample;
typedef struct smith_tuple_i32__f32 {
__INT32_TYPE__ _0;
float _1;
} walk_Pair;
static __SIZE_TYPE__ walk_name_length(Field f) {
return countof(f.name);
}
static bool walk_is_floating(Field f) {
return Float(f.type);
}
static Field walk_first_float(Field a, Field b) {
return walk_is_floating(a) ? a : b;
} [ [ clang :: unavailable ( "walk::show is a generic function: call it from a .smith module" ) ] ] void walk_show ( void ) ; [ [ clang :: unavailable ( "walk::total_length is a generic function: call it from a .smith module" ) ] ] void walk_total_length ( void ) ; [ [ clang :: unavailable ( "walk::sum_floats is a generic function: call it from a .smith module" ) ] ] void walk_sum_floats ( void ) ; [ [ clang :: unavailable ( "walk::count_integers is a generic function: call it from a .smith module" ) ] ] void walk_count_integers ( void ) ; [ [ clang :: unavailable ( "walk::float_name is a generic function: call it from a .smith module" ) ] ] void walk_float_name ( void ) ; [ [ clang :: unavailable ( "walk::second_length is a generic function: call it from a .smith module" ) ] ] void walk_second_length ( void ) ;
void walk_run() __asm__("_walk::run");
void walk_run() {
walk_Sample s = {1, 2.5f, 3.0, 4};
walk_Pair p = {7, 1.5f};__extension__({extern void walk_show__Sample(walk_Sample*x)__asm__("_walk::show(walk_Sample)");walk_show__Sample(&s);});__extension__({extern void walk_show__Pair(walk_Pair*x)__asm__("_walk::show(walk_Pair)");walk_show__Pair(&p);});
printf("%zu %zu\n",__extension__({extern __SIZE_TYPE__ walk_total_length__Sample(void)__asm__("_walk::total_length(walk_Sample)");walk_total_length__Sample();}),__extension__({extern __SIZE_TYPE__ walk_total_length__Pair(void)__asm__("_walk::total_length(walk_Pair)");walk_total_length__Pair();}));
printf("%g %g %zu %zu\n",__extension__({extern double walk_sum_floats__Sample(walk_Sample*x)__asm__("_walk::sum_floats(walk_Sample)");walk_sum_floats__Sample(&s);}),__extension__({extern double walk_sum_floats__Pair(walk_Pair*x)__asm__("_walk::sum_floats(walk_Pair)");walk_sum_floats__Pair(&p);}),__extension__({extern __SIZE_TYPE__ walk_count_integers__Sample(void)__asm__("_walk::count_integers(walk_Sample)");walk_count_integers__Sample();}),__extension__({extern __SIZE_TYPE__ walk_count_integers__Pair(void)__asm__("_walk::count_integers(walk_Pair)");walk_count_integers__Pair();}));
printf("%s %s %zu\n",__extension__({extern const char*walk_float_name__Sample(void)__asm__("_walk::float_name(walk_Sample)");walk_float_name__Sample();}),__extension__({extern const char*walk_float_name__Pair(void)__asm__("_walk::float_name(walk_Pair)");walk_float_name__Pair();}),__extension__({extern __SIZE_TYPE__ walk_second_length__Sample(void)__asm__("_walk::second_length(walk_Sample)");walk_second_length__Sample();}));
}
typedef struct smith_field{struct smith_array_char_n{char*data;__SIZE_TYPE__ count[1];}name;unsigned __int128 type;struct smith_array_char_n doc;}Field;[[maybe_unused]]static inline Field smith_field_make(char*name,__SIZE_TYPE__ length,unsigned __int128 kind,char*doc,__SIZE_TYPE__ doc_length){return(Field){{name,{length}},kind,{doc,{doc_length}}};}extern void walk_show__Sample(walk_Sample*x)__asm__("_walk::show(walk_Sample)");
[[gnu::weak]][[gnu::visibility("hidden")]]void walk_show__Sample(walk_Sample*x){printf("\n");}
extern void walk_show__Pair(walk_Pair*x)__asm__("_walk::show(walk_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]void walk_show__Pair(walk_Pair*x){printf("\n");}
extern __SIZE_TYPE__ walk_total_length__Sample(void)__asm__("_walk::total_length(walk_Sample)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ walk_total_length__Sample(void){__SIZE_TYPE__ total=0;return total;}
extern __SIZE_TYPE__ walk_total_length__Pair(void)__asm__("_walk::total_length(walk_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ walk_total_length__Pair(void){__SIZE_TYPE__ total=0;return total;}
extern double walk_sum_floats__Sample(walk_Sample*x)__asm__("_walk::sum_floats(walk_Sample)");
[[gnu::weak]][[gnu::visibility("hidden")]]double walk_sum_floats__Sample(walk_Sample*x){double total=0;return total;}
extern double walk_sum_floats__Pair(walk_Pair*x)__asm__("_walk::sum_floats(walk_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]double walk_sum_floats__Pair(walk_Pair*x){double total=0;return total;}
extern __SIZE_TYPE__ walk_count_integers__Sample(void)__asm__("_walk::count_integers(walk_Sample)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ walk_count_integers__Sample(void){__SIZE_TYPE__ n=0;return n;}
extern __SIZE_TYPE__ walk_count_integers__Pair(void)__asm__("_walk::count_integers(walk_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ walk_count_integers__Pair(void){__SIZE_TYPE__ n=0;return n;}
extern const char*walk_float_name__Sample(void)__asm__("_walk::float_name(walk_Sample)");
[[gnu::weak]][[gnu::visibility("hidden")]]const char*walk_float_name__Sample(void){constexpr Field f=walk_first_float(smith_call_failed,smith_call_failed);return f.name.data;}
extern const char*walk_float_name__Pair(void)__asm__("_walk::float_name(walk_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]const char*walk_float_name__Pair(void){constexpr Field f=walk_first_float(smith_call_failed,smith_call_failed);return f.name.data;}
extern __SIZE_TYPE__ walk_second_length__Sample(void)__asm__("_walk::second_length(walk_Sample)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ walk_second_length__Sample(void){const Field*all=smith_call_failed;return all[1].name.count[0];}

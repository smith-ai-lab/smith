#include <stdio.h>
#include "lib.h"

typedef struct {
__INT32_TYPE__ id;
float weight;
double scale;
} main_Sample;
__INT32_TYPE__ main(void) {
main_Sample s = {1, 2.5f, 4};
printf("%g\n",__extension__({extern double lib_sum_floats__Sample(main_Sample*x)__asm__("_lib::sum_floats(main_Sample)");lib_sum_floats__Sample(&s);}));
return 0;
}
typedef struct smith_field{struct smith_array_char_n{char*data;__SIZE_TYPE__ count[1];}name;unsigned __int128 type;struct smith_array_char_n doc;}Field;[[maybe_unused]]static inline Field smith_field_make(char*name,__SIZE_TYPE__ length,unsigned __int128 kind,char*doc,__SIZE_TYPE__ doc_length){return(Field){{name,{length}},kind,{doc,{doc_length}}};}extern double lib_sum_floats__Sample(main_Sample*x)__asm__("_lib::sum_floats(main_Sample)");
[[gnu::weak]][[gnu::visibility("hidden")]]double lib_sum_floats__Sample(main_Sample*x){double total=0;return total;}

#include "lib.h"

typedef struct {
__INT32_TYPE__ id;
float weight;
__UINT8_TYPE__ flag;
} main_Sample;
__INT32_TYPE__ main(void) {
main_Sample s = {1, 2.5f, 4};__extension__({extern void lib_show__Sample(main_Sample*x)__asm__("_lib::show(main_Sample)");lib_show__Sample(&s);});
return 0;
}
typedef struct smith_field{struct smith_array_char_n{char*data;__SIZE_TYPE__ count[1];}name;unsigned __int128 type;}Field;[[maybe_unused]]static inline Field smith_field_make(char*name,__SIZE_TYPE__ length,unsigned __int128 kind){return(Field){{name,{length}},kind};}extern void lib_show__Sample(main_Sample*x)__asm__("_lib::show(main_Sample)");
[[gnu::weak]][[gnu::visibility("hidden")]]void lib_show__Sample(main_Sample*x){printf("\n");}

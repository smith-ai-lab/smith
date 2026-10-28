#include "reflect.h"

typedef union {
__INT32_TYPE__ a;
float b;
} main_Either;
typedef struct {
__INT32_TYPE__ a;
struct {
__INT32_TYPE__ b;
__INT32_TYPE__ c;
};
} main_Anonymous;
typedef struct {
__INT32_TYPE__ a;
} main_One;
__INT32_TYPE__ main(void) {
__INT32_TYPE__ total = 0;
total +=__extension__({extern __INT32_TYPE__ reflect_count_int__I32(void)__asm__("_reflect::count_int(__INT32_TYPE__)");reflect_count_int__I32();});
total +=__extension__({extern __INT32_TYPE__ reflect_count_union__Either(void)__asm__("_reflect::count_union(main_Either)");reflect_count_union__Either();});
total +=__extension__({extern __INT32_TYPE__ reflect_count_anonymous__Anonymous(void)__asm__("_reflect::count_anonymous(main_Anonymous)");reflect_count_anonymous__Anonymous();});
main_One one = {1};
total +=__extension__({extern __INT32_TYPE__ reflect_out_of_range__One(main_One*x)__asm__("_reflect::out_of_range(main_One)");reflect_out_of_range__One(&one);});
total +=__extension__({extern __INT32_TYPE__ reflect_not_constant__One(main_One*x,__INT32_TYPE__ n)__asm__("_reflect::not_constant(main_One)");reflect_not_constant__One(&one,0);});__extension__({extern void reflect_odd_name__One(void)__asm__("_reflect::odd_name(main_One)");reflect_odd_name__One();});
return total;
}
extern __INT32_TYPE__ reflect_count_int__I32(void)__asm__("_reflect::count_int(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ reflect_count_int__I32(void){return smith_call_failed;}
extern __INT32_TYPE__ reflect_count_union__Either(void)__asm__("_reflect::count_union(main_Either)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ reflect_count_union__Either(void){return smith_call_failed;}
extern __INT32_TYPE__ reflect_count_anonymous__Anonymous(void)__asm__("_reflect::count_anonymous(main_Anonymous)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ reflect_count_anonymous__Anonymous(void){return smith_call_failed;}
extern __INT32_TYPE__ reflect_out_of_range__One(main_One*x)__asm__("_reflect::out_of_range(main_One)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ reflect_out_of_range__One(main_One*x){return x->smith_call_failed;}
extern __INT32_TYPE__ reflect_not_constant__One(main_One*x,__INT32_TYPE__ n)__asm__("_reflect::not_constant(main_One)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ reflect_not_constant__One(main_One*x,__INT32_TYPE__ n){return x->smith_call_failed;}
typedef struct smith_field{struct smith_array_char_n{char*data;__SIZE_TYPE__ count[1];}name;unsigned __int128 type;struct smith_array_char_n doc;}Field;[[maybe_unused]]static inline Field smith_field_make(char*name,__SIZE_TYPE__ length,unsigned __int128 kind,char*doc,__SIZE_TYPE__ doc_length){return(Field){{name,{length}},kind,{doc,{doc_length}}};}extern void reflect_odd_name__One(void)__asm__("_reflect::odd_name(main_One)");
[[gnu::weak]][[gnu::visibility("hidden")]]void reflect_odd_name__One(void){const char*name=smith_call_failed.label;}

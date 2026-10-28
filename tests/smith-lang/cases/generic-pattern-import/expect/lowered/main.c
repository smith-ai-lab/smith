#include <stdio.h>
#include "lists.h"
struct smith_struct_List_i32 {__INT32_TYPE__ * items ; __SIZE_TYPE__ count ; };
typedef struct smith_struct_List_i32 main_Ints;
__INT32_TYPE__ main(void) {
__INT32_TYPE__ storage[4] = {};
main_Ints a = {.items = storage};smith_call_failed;smith_call_failed;
auto b = lists_filled(10);smith_call_failed;
printf("%zu %zu %zu %zu\n", a.count,smith_call_failed, b.count,smith_call_failed);
return 0;
}

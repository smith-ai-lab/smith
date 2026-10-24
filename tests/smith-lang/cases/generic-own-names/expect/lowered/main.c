#include "smith/tools.h"
#include <stdio.h>

int main() {
const __INT32_TYPE__ value = 5;
__INT32_TYPE__ a =smith_call_failed;
__INT32_TYPE__ b =smith_call_failed;
__INT32_TYPE__ c =smith_call_failed;
__INT32_TYPE__ d =__extension__({extern __INT32_TYPE__ smith_tools_table_sum__(void)__asm__("_smith::tools::table_sum()");smith_tools_table_sum__();});
__INT32_TYPE__ e =smith_call_failed;
printf("%d %d %d %d %d\n", a, b, c, d, e);
return 0;
}
extern __INT32_TYPE__ smith_tools_table_sum__(void)__asm__("_smith::tools::table_sum()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ smith_tools_table_sum__(void){__INT32_TYPE__ table[]={100};__INT32_TYPE__ sum=0;for(__SIZE_TYPE__ i=0;i<sizeof table/sizeof table[0];i++)sum+=table[i];return sum;}

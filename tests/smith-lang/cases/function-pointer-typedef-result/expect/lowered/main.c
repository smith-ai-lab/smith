#include "hook/table.h"
#include <stdio.h>

__INT32_TYPE__ main(void) {
hook_table_Plain plain = hook_table_plain();
printf("%llu %llu %llu %llu\n", (unsigned long long)plain(21), (unsigned long long)hook_table_call(plain, 4),
(unsigned long long)hook_table_chosen_value(5), (unsigned long long)hook_table_hook(8));
return 0;
}

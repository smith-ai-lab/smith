#include <stdio.h>

static __INT32_TYPE__ main_plain(void) {
errdefer puts("never");
return 0;
}
__INT32_TYPE__ main(void) {
return main_plain();
}

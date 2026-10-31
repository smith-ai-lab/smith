#include <stdio.h>

__INT32_TYPE__ main(void) {
__INT32_TYPE__ (*seven)() = { return 7; };
void (*hello)() = { printf("hello\n"); };
hello();
printf("%d\n", seven());
return 0;
}

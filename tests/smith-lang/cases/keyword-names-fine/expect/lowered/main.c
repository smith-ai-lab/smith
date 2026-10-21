#include <stdio.h>
__UINT32_TYPE__ const first = 1;
__UINT32_TYPE__ static second = 2;
unsigned long third = 3;
long double fourth = 4;
double *restrict pointer = nullptr;
__UINT8_TYPE__ alignas(4) aligned = 5;
__INT32_TYPE__ main() {
__INT32_TYPE__ sum = (__INT32_TYPE__)first + (__INT32_TYPE__)second + (__INT32_TYPE__)third + (__INT32_TYPE__)fourth + sizeof(__UINT8_TYPE__) + sizeof(__UINT16_TYPE__);
printf("%d %d\n", sum, aligned);
return 0;
}

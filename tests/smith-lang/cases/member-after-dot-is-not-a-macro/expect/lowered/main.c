#include "lens.h"
#include <stdio.h>
typedef struct {
__INT32_TYPE__ vector_length;
} Lens;
int main() {
Lens lens = {.vector_length = 3};
Lens *p = &lens;
printf("%d %d\n", lens.vector_length, p->vector_length);
return 0;
}

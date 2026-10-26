#include "errors.h"
#include <stdio.h>

int main(void) {
printf("%d\n", errors_current() >= 0 ? 1 : 0);
return 0;
}

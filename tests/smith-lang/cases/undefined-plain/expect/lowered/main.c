#include <stdio.h>
static int undefined = 7;
int main(void) {
int copy = undefined;
undefined = copy + 1;
printf("%d\n", undefined);
return 0;
}

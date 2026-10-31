#include <stdio.h>
int apply(int (*f)(int), int x) {
return f(x);
}
int main(void) {
int r = apply((int a) { return a + 1; }, 4);
printf("%d\n", r);
return 0;
}

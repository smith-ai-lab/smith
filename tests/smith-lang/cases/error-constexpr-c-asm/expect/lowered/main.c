#include <stdio.h>
static int forty() {
return 40 + 2;
}
int main() {
int r;
asm("mov %w0, %1" : "=r"(r) : "i"(forty()));
printf("%d\n", r);
return 0;
}

#include <stdio.h>

static __INT32_TYPE__ main_early(__INT32_TYPE__ n) {
defer puts("leave early");
if (n > 0) return 1;
puts("not early");
return 0;
}
static void main_jump(void) {
{
defer puts("cleanup before the jump");
goto out;
}
out:
puts("after the jump");
}
__INT32_TYPE__ main(void) {
{
defer puts("first deferred, runs last");
defer puts("second deferred, runs first");
puts("body");
}
for (__INT32_TYPE__ i = 0; i < 3; i++) {
defer printf("end of iteration %d\n", i);
if (i == 0) continue;
if (i == 2) break;
puts("middle");
}
{
defer {
puts("block a");
puts("block b");
}
puts("in block");
}
{
defer puts("outer");
{
defer puts("inner");
puts("nested body");
}
puts("outer body");
}
main_early(1);
main_early(0);
main_jump();
return 0;
}

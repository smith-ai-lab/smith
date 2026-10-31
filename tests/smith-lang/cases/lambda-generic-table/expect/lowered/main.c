#include <stdio.h>

typedef struct {
const char *path;
__INT32_TYPE__ (*call)(__INT32_TYPE__);
} main_Route;
static __INT32_TYPE__ main_add_one(__INT32_TYPE__ x) {
return x + 1;
}
static __INT32_TYPE__ main_triple(__INT32_TYPE__ x) {
return x * 3;
} [ [ clang :: unavailable ( "main::route is a generic function: call it from a .smith module" ) ] ] void main_route ( void ) ;
__INT32_TYPE__ main(void) {
main_Route table[2] = {smith_call_failed,smith_call_failed};
for (__SIZE_TYPE__ i = 0; i < 2; i++) {
printf("%s %d\n", table[i].path, table[i].call(5));
}
return 0;
}

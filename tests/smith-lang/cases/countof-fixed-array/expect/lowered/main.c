#include <stdio.h>

struct main_Grid {
__UINT32_TYPE__ cells[6];
};
typedef struct main_Grid main_Grid;
static __UINT32_TYPE__ main_plain() {
__UINT32_TYPE__ xs[] = {1, 2, 3};
return (__UINT32_TYPE__)countof(xs);
}
static __UINT32_TYPE__ main_outer() {
float m[3][5] = {};
return (__UINT32_TYPE__)countof(m) * 10 + (__UINT32_TYPE__)countof(m[0]);
}
static __UINT32_TYPE__ main_shaped(__UINT32_TYPE__ xs[static 4]) {
return (__UINT32_TYPE__)countof(xs);
} [ [ clang :: unavailable ( "main::pack is a generic function: call it from a .smith module" ) ] ] void main_pack ( void ) ; [ [ clang :: unavailable ( "main::member is a generic function: call it from a .smith module" ) ] ] void main_member ( void ) ;
__INT32_TYPE__ main(void) {
__UINT32_TYPE__ four[4] = {};
struct main_Grid g = {};
printf("%u %u %u %u %u\n", main_plain(), main_outer(), main_shaped(four),smith_call_failed,smith_call_failed);
return 0;
}

#include <stdio.h>
struct Point {
__INT32_TYPE__ x;
float y;
};
struct Grid {
__INT32_TYPE__ cells[2][3];
struct Point origin;
};
constexpr struct Grid grid = {.cells = {{1, 2, 3}, {4, 5, -6}}, .origin = {7, 0.5f}};
constexpr double ratio = 0.1;
constexpr char name[] = "hi";
__INT32_TYPE__ counter = 3;
static __INT32_TYPE__ twice(__INT32_TYPE__ n) {
return n * 2;
}
int main(void) {
printf("%d\n", twice(21));
return 0;
}

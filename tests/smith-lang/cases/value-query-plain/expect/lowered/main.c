#include <stdio.h>
static int f(int n) { return n * 2; }
struct C { int count; };
constexpr struct C cfg = {.count = 3};
int main() { { puts("small"); }
return 0;
}

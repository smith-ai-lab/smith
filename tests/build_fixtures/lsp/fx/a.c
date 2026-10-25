#include "lib.h"
[[smith::pure]] int bad(int x) { return noisy(x); }
int main(void) { return bad(1); }

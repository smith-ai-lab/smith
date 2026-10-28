#include "lib.h"
[[std::pure]] int bad(int x) { return noisy(x); }
int main(void) { return bad(1); }

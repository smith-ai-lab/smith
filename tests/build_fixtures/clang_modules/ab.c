#include "inc/a.h"
#include "inc/b.h"
int a_value(void) { return 1; }
int b_value(void) { return a_value() + 1; }

#include "pick/inl.h"
#include <stdio.h>

int main(void) {
pick_inl_show();
printf("same in main: %d\n", pick_inl_flavor());
return 0;
}

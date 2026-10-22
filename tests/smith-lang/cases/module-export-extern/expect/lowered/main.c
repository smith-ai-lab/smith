#include "cfg/limits.h"
#include <stdio.h>
int main(void) {
printf("%u %u\n", cfg_limits_max_users, cfg_limits_users_allowed());
return 0;
}

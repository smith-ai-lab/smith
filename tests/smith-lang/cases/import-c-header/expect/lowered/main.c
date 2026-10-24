#include <math.h>
#include <sys/wait.h>
#include <stdio.h>
#include "local.h"

int main() {
float root = sqrtf(81.0f);
int status = 256 * 3;
printf("%.1f %d %d eof=%d %d\n", (double)root, WIFEXITED(status), WEXITSTATUS(status), EOF,
local_value());
return 0;
}

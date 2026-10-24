#include "log/fmt.h"
#include <stdio.h>

int main() {
log_fmt_say("%d\n", 7);
log_fmt_said("done\n");
printf("%d\n", 1);
return 0;
}

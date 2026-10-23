#include "smith/io.h"
#include <stdio.h>

__INT32_TYPE__ helper(__INT32_TYPE__ x) {
return x * 2;
}
static __INT32_TYPE__ smith_user_main(SmithIo *io) {
printf("main has %d argument\n", io->count);
if (io->count == 1) {
return helper(21);
}
return 0;
}
int main (int argc, char * * argv) { SmithIo io = smith_io_make (argc, argv); int status = smith_user_main (&io); smith_io_close (&io); return status; }

#include "smith/io.h"
#include <stdio.h>

static __INT32_TYPE__ main_helper(__INT32_TYPE__ x) {
return x + 1;
}
static __INT32_TYPE__ smith_user_main(SmithIo *io) {
printf("main has %d argument\n", main_helper(io->count - 1));
return 4;
}
int main (int argc, char * * argv) { SmithIo io = smith_io_make (argc, argv); int status = smith_user_main (&io); smith_io_close (&io); return status; }

#include "smith/io.h"
#include <stdio.h>

__INT32_TYPE__ main(__INT32_TYPE__ argc, char *argv[]) {
SmithIo io = smith_io_make(argc, argv);
printf("made the io with %d argument\n", io.count);
smith_io_close(&io);
return 0;
}

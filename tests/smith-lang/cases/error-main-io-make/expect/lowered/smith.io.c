#include <stdio.h>

typedef struct {
__INT32_TYPE__ count;
} SmithIo;
SmithIo smith_io_make(__INT32_TYPE__ argc, char **argv);
SmithIo smith_io_make(__INT32_TYPE__ argc, char **argv) {
(void)argv;
return (SmithIo){.count = argc};
}
void smith_io_close(SmithIo *io);
void smith_io_close(SmithIo *io) {
printf("closed after %d argument\n", io->count);
}

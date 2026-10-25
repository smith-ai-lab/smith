#include <smith/io.h>

#include <stdio.h>

int main(int argc, char **argv) {
    SmithIo io = smith_io_make(argc, argv);
    printf("seed %d\n", io.seed != 0);
    return 0;
}

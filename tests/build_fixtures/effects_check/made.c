#include <smith/io.h>

int main(int argc, char **argv) {
    SmithIo io = smith_io_make(argc, argv);
    SmithIo fake = smith_io_make_test((SmithIoTest){});
    (void)fake;
    smith_io_close(&io);
    return 0;
}

SmithIo elsewhere(int argc, char **argv) {
    return smith_io_make(argc, argv);
}

SmithIo test_helper(void) {
    return smith_io_make_test((SmithIoTest){});
}

#include "local_io.h"

int main(int argc, char **argv) {
    LocalIo io = local_io_make(argc, argv);
    LocalIo fake = local_io_make_test((LocalIoTest){});
    (void)fake;
    local_io_close(&io);
    return 0;
}

LocalIo elsewhere(int argc, char **argv) {
    return local_io_make(argc, argv);
}

LocalIo test_helper(void) {
    return local_io_make_test((LocalIoTest){});
}

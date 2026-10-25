#include <smith/io.h>
#include <smith/test.h>

void test_io_makes(void) {
    SmithIo io = smith_io_make(0, nullptr);
    smith_check(io.seed != 0);
    smith_io_close(&io);
}

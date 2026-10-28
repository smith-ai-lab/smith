#include "smith/io.h"

static __INT32_TYPE__ smith_user_main(SmithIo *io) {
SmithIo other = smith_io_make(1, nullptr);
return other.count - io->count;
}
int main (int argc, char * * argv) { SmithIo io = smith_io_make (argc, argv); int status = smith_user_main (&io); smith_io_close (&io); return status; }

#include "local_io.h"

typedef struct {
    LocalFiles *files;
} Bundle;

static LocalIo *global_io;
LocalClock *global_clock;
static Bundle bundle;
static int number;

void use(void) {
    static LocalConsole console;
    static int counter;
    (void)console;
    counter++;
}

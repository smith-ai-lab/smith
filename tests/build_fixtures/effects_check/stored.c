#include <smith/io.h>

typedef struct {
    SmithFiles *files;
} Bundle;

static SmithIo *global_io;
SmithClock *global_clock;
static Bundle bundle;
static int number;

void use(void) {
    static SmithConsole console;
    static int counter;
    (void)console;
    counter++;
}

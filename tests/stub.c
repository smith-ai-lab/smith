// A stand-in compiler for the routing tests: it reports its name, the name it was run as and every argument, so a test
// can see exactly what smith forwarded. STUB_EXIT sets its exit status and STUB_SIGNAL makes it kill itself.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    printf("%s argv0=%s\n", NAME, argv[0]);
    for (int i = 1; i < argc; i++) printf("[%s]\n", argv[i]);
    fprintf(stderr, "%s-stderr\n", NAME);
    const char *signal_number = getenv("STUB_SIGNAL");
    if (signal_number != NULL) kill(getpid(), atoi(signal_number));
    const char *code = getenv("STUB_EXIT");
    return code != NULL ? atoi(code) : 0;
}

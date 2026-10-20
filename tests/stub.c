// A stand-in compiler for the routing tests: it reports its name, the name it was run as and every argument, so a test
// can see exactly what smith forwarded. STUB_EXIT sets its exit status and STUB_SIGNAL makes it kill itself.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// The number in an environment variable the tests set, or fallback when it is unset.
static int stub_env_number(const char *name, int fallback) {
    const char *text = getenv(name);
    return text != nullptr ? (int)strtol(text, nullptr, 10) : fallback;
}

int main(int argc, char **argv) {
    printf("%s argv0=%s\n", NAME, argv[0]);
    for (int i = 1; i < argc; i++)
        printf("[%s]\n", argv[i]);
    fprintf(stderr, "%s-stderr\n", NAME);
    if (getenv("STUB_SIGNAL") != nullptr) kill(getpid(), stub_env_number("STUB_SIGNAL", 0));
    return stub_env_number("STUB_EXIT", 0);
}

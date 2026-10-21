// A stand-in compiler for the routing tests: it reports its name, the name it was run as and every argument, so a test
// can see exactly what smith forwarded. STUB_EXIT sets its exit status and STUB_SIGNAL makes it kill itself.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// The number in an environment variable the tests set, or fallback when it is unset.
static int stub_env_number(const char *name, int fallback) {
    const char *text = getenv(name);
    return text != nullptr ? (int)strtol(text, nullptr, 10) : fallback;
}

int main(int argc, char **argv) {
    // As xcrun, STUB_SDK is the whole answer to `--show-sdk-path`; STUB_XCRUN_FAIL makes it fail.
    if (strcmp(NAME, "xcrun") == 0 && getenv("STUB_SDK") != nullptr && argc == 2) {
        puts(getenv("STUB_SDK"));
        return getenv("STUB_XCRUN_FAIL") != nullptr ? 1 : 0;
    }
    // STUB_SHOW_ENV adds the two variables smith sets on macOS, so a test sees them.
    if (getenv("STUB_SHOW_ENV") != nullptr) {
        const char *sdk = getenv("SDKROOT");
        const char *config = getenv("CLANG_NO_DEFAULT_CONFIG");
        printf("env SDKROOT=%s CLANG_NO_DEFAULT_CONFIG=%s\n", sdk != nullptr ? sdk : "(unset)",
               config != nullptr ? config : "(unset)");
    }
    printf("%s argv0=%s\n", NAME, argv[0]);
    for (int i = 1; i < argc; i++)
        printf("[%s]\n", argv[i]);
    fprintf(stderr, "%s-stderr\n", NAME);
    if (getenv("STUB_SIGNAL") != nullptr) kill(getpid(), stub_env_number("STUB_SIGNAL", 0));
    return stub_env_number("STUB_EXIT", 0);
}

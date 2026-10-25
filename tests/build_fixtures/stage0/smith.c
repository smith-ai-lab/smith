// A stand-in for a release's smith: `version` prints a line; `build` records its arguments and the smith-cc found first
// on PATH into $STAGE0_OUT.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "version") == 0) {
        puts("fake smith");
        return 0;
    }
    if (argc < 2 || strcmp(argv[1], "build") != 0) return 2;
    FILE *out = fopen(getenv("STAGE0_OUT"), "w");
    if (out == nullptr) return 1;
    fputs("build", out);
    for (int i = 2; i < argc; i++) fprintf(out, " %s", argv[i]);
    fputc('\n', out);
    const char *path = getenv("PATH");
    for (const char *at = path; at != nullptr && *at != '\0';) { // each pass moves past one folder of PATH
        const char *end = strchr(at, ':');
        int size = end != nullptr ? (int)(end - at) : (int)strlen(at);
        char candidate[4096];
        snprintf(candidate, sizeof candidate, "%.*s/smith-cc", size, at);
        if (access(candidate, X_OK) == 0) {
            fprintf(out, "%s\n", candidate);
            break;
        }
        at = end != nullptr ? end + 1 : nullptr;
    }
    fclose(out);
    return 0;
}

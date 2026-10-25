// A stand-in for `gh release download <tag> --pattern P ... --dir D --clobber`: the "releases" are folders under
// $GH_DIR/<tag>, every call is appended to $GH_LOG, and a tag with no folder fails as gh does.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int copy(const char *from, const char *to) {
    FILE *in = fopen(from, "rb");
    FILE *out = fopen(to, "wb");
    if (in == nullptr || out == nullptr) return 1;
    char buffer[4096];
    for (size_t got = 1; got > 0;) { // until the end of the file
        got = fread(buffer, 1, sizeof buffer, in);
        if (got > 0) fwrite(buffer, 1, got, out);
    }
    fclose(in);
    fclose(out);
    return 0;
}

int main(int argc, char **argv) {
    FILE *log = fopen(getenv("GH_LOG"), "a");
    if (log != nullptr) {
        fputs("gh", log);
        for (int i = 1; i < argc; i++) fprintf(log, " %s", argv[i]);
        fputc('\n', log);
        fclose(log);
    }
    if (argc < 4 || strcmp(argv[1], "release") != 0 || strcmp(argv[2], "download") != 0) return 0;
    const char *dir = ".";
    for (int i = 4; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--dir") == 0) dir = argv[i + 1];
    }
    char from[4096];
    char to[4096];
    snprintf(from, sizeof from, "%s/%s", getenv("GH_DIR"), argv[3]);
    FILE *release = fopen(from, "r");
    if (release == nullptr) return 1;
    fclose(release);
    for (int i = 4; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--pattern") != 0) continue;
        snprintf(from, sizeof from, "%s/%s/%s", getenv("GH_DIR"), argv[3], argv[i + 1]);
        snprintf(to, sizeof to, "%s/%s", dir, argv[i + 1]);
        if (copy(from, to) != 0) return 1;
    }
    return 0;
}

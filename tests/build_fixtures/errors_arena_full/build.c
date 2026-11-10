#include <smith/build.h>
#include <stdio.h>

// Reads a file of 5 MB twenty times: every read stays in the 64 MB arena, so the build overflows it (#3032).
void build(SmithBuild *b) {
    FILE *file = fopen("big.txt", "w");
    for (int i = 0; file != nullptr && i < 5 * 1024; i++) {
        for (int j = 0; j < 15; j++) fputs("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n", file);
        fputs("end\n", file);
    }
    if (file != nullptr) fclose(file);
    for (int i = 0; i < 20; i++) smith_lines(b, "big.txt");
    SmithStep s = {.command = (const char *[]){"touch", "$out", nullptr}, .outputs = (const char *[]){"x", nullptr}};
    smith_step(b, &s);
    smith_default(b, (const char *[]){"x", nullptr});
}

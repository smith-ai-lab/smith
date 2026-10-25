#include <smith/build.h>

void build(SmithBuild *b) {
    smith_write(b, "deep.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"src/**/*.c", nullptr}), "%s\n"), ""));
    smith_write(b, "hidden.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"src/**/*.h", nullptr}), "%s\n"), ""));
    smith_write(b, "overlap.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"src/**/*.c", "src/x/*.c", "src/*.c", "**/b.c", nullptr}), "%s\n"), ""));
    smith_write(b, "dot.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"src/.*/*.c", "src/**/.hid/*.h", nullptr}), "%s\n"), ""));
    smith_write(b, "star.txt", smith_join(b, smith_map(b, smith_glob(b, (const char *[]){"*/a.c", "s?c/x/[y]/*.c", nullptr}), "%s\n"), ""));
    smith_default(b, smith_list(b, nullptr));
}

#include <smith/build.h>

void build(SmithBuild *b) {
    const char *const *selected = (const char *[]){nullptr};
    const char *const *joined = smith_cat(b, selected, (const char *[]){"--function=x", nullptr}, nullptr);
    smith_write(b, "joined.txt", smith_join(b, joined, " "));
}

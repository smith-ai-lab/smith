#include <smith/build.h>

void build(SmithBuild *b) {
    const char *warn = smith_option_value(b, "W", "-Wall", "a warning flag");
    SmithStep check = {
        .command = (const char *[]){"cc", "-std=gnu23", warn, "-Werror", "-fsyntax-only", "-MMD", "-MF",
                                    "build/check/a.d", "-MT", "build/check/a.stamp", "a.c", nullptr},
        .inputs = (const char *[]){"a.c", nullptr},
        .stamp = "build/check/a.stamp",
        .depfile = "build/check/a.d",
        .description = "check a",
    };
    smith_step(b, &check);
    SmithStep same = {
        .command = (const char *[]){"cc", "-std=gnu23", warn, "-Werror", "-fsyntax-only", "-MMD", "-MF",
                                    "build/check/a.d", "-MT", "build/check/a.stamp", "a.c", nullptr},
        .inputs = (const char *[]){"a.c", nullptr},
        .stamp = "build/check/a.stamp",
        .depfile = "build/check/a.d",
        .description = "check a",
    };
    smith_step(b, &same);
    smith_alias(b, "syntax", check.outputs);
    smith_default(b, check.outputs);
}

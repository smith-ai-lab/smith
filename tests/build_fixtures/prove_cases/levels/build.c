// A program whose sources compile with different optimization levels (hot.c at -O2), proved as one unit.
#include <smith/build.h>
#include <stdlib.h>

void build(SmithBuild *b) {
    const char *repo = getenv("SMITH_REPO");
    const char *include = smith_fmt(b, "%s/include", repo != nullptr ? repo : "..");
    SmithObject hot = {.compiler = "smith-cc",
                       .source = "hot.c",
                       .output = "build/hot.o",
                       .flags = smith_list(b, "-std=gnu23", "-I", include, "-O2", nullptr)};
    SmithObject main_object = {.compiler = "smith-cc",
                               .source = "main.c",
                               .output = "build/main.o",
                               .flags = smith_list(b, "-std=gnu23", "-I", include, nullptr)};
    smith_object(b, &hot);
    smith_object(b, &main_object);
    SmithProgram app = {.linker = "smith-cc",
                        .output = "build/app",
                        .objects = (const SmithObject *[]){&hot, &main_object, nullptr}};
    smith_program(b, &app);
    smith_default(b, app.step.outputs);
}

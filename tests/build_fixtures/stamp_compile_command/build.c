#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject util = {.compiler = "cc", .source = "util.c", .output = "build/util.o"};
    smith_object(b, &util);
    smith_compile_command(b, "cc", "tests/a.c", (const char *[]){"-std=gnu23", "-Iinc", nullptr});
    smith_compile_command(b, "cc", "util.c", (const char *[]){"-DIGNORED", nullptr});
    smith_compile_command(b, "cc", "tests/b.c", nullptr);
    smith_compile_command(b, "cc", "tests/a.c", (const char *[]){"-DIGNORED", nullptr});
    smith_compile_commands(b, "compile_commands.json");
    smith_default(b, util.step.outputs);
}

#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep copy = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = (const char *[]){"my input.txt", nullptr},
        .outputs = (const char *[]){"my copy.txt", nullptr},
    };
    smith_step(b, &copy);
    SmithStep dollar = {
        .command = (const char *[]){"sh", "-c", "printf '%s\\n' \"$0\" > \"$1\"", "a$b 'q'", "$out", nullptr},
        .outputs = (const char *[]){"dollar file.txt", nullptr},
    };
    smith_step(b, &dollar);
    smith_default(b, smith_cat(b, copy.outputs, dollar.outputs, nullptr));
}

#include <smith/build.h>

void build(SmithBuild *b) {
    const char *dir = smith_package(b, &(SmithPackage){
                                           .url = smith_option_value(b, "URL", nullptr, "the package"),
                                           .hash = smith_option_value(b, "HASH", nullptr, "its hash"),
                                       });
    smith_write(b, "path.txt", dir);
    SmithStep copy = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = smith_list(b, smith_fmt(b, "%s/hello.txt", dir), nullptr),
        .outputs = (const char *[]){"out.txt", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}

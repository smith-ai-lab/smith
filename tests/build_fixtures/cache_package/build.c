#include <smith/build.h>

void build(SmithBuild *b) {
    const char *dir = smith_package(b, &(SmithPackage){
                                           .url = smith_option_value(b, "URL", nullptr, "the package"),
                                           .hash = smith_option_value(b, "HASH", nullptr, "its hash"),
                                       });
    smith_write(b, "path.txt", dir);
    smith_default(b, smith_list(b, nullptr));
}

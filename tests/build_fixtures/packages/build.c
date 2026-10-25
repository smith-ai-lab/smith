#include <smith/build.h>
#include <stdio.h>
#include <stdlib.h>

void build(SmithBuild *b) {
    smith_build_PackageResult found = smith_package(b, &(SmithPackage){
                                           .url = smith_option_value(b, "URL", nullptr, "the package"),
                                           .hash = smith_option_value(b, "HASH", nullptr, "its hash"),
                                       });
    if (found.tag == smith_enum_result_const_char_ptr__SmithPackageError_tag_error) {
        fprintf(stderr, "smith: package: %s\n", found.error.message);
        exit(2);
    }
    if (found.tag != smith_enum_result_const_char_ptr__SmithPackageError_tag_ok) __builtin_unreachable();
    const char *dir = found.ok;
    smith_write(b, "path.txt", dir);
    SmithStep copy = {
        .command = (const char *[]){"cp", "$in", "$out", nullptr},
        .inputs = smith_list(b, smith_fmt(b, "%s/hello.txt", dir), nullptr),
        .outputs = (const char *[]){"out.txt", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}

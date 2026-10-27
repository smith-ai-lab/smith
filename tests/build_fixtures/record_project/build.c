#include <smith/build.h>

void build(SmithBuild *b) {
    smith_app(b, &(SmithApp){.name = "calc"});
    smith_app_tests(b, &(SmithAppTests){.inputs = smith_list(b, "build/debug/calc", nullptr)});
}

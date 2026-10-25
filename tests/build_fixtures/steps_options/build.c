#include <smith/build.h>

void build(SmithBuild *b) {
    bool loud = smith_option(b, "loud", "print in capitals");
    const char *name = smith_option_value(b, "name", "world", "who to greet");
    SmithStep greet = {
        .command = (const char *[]){"sh", "-c", "echo \"$0 $1\" > \"$2\"", name, loud ? "loud" : "quiet", "$out", nullptr},
        .outputs = (const char *[]){"greeting", nullptr},
    };
    smith_step(b, &greet);
    smith_default(b, greet.outputs);
}

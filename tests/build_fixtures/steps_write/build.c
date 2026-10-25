#include <smith/build.h>

void build(SmithBuild *b) {
    smith_write(b, "gen/deep/list.txt", smith_option_value(b, "text", "one", "the file's text"));
    SmithStep copy = {
        .command = (const char *[]){"sh", "-c", "cp \"$0\" \"$1\"; echo x >> copies", "$in", "$out", nullptr},
        .inputs = (const char *[]){"gen/deep/list.txt", nullptr},
        .outputs = (const char *[]){"copy.txt", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}

#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep generate = {
        .command = (const char *[]){"sh", "-c", "if [ \"$(cat \"$0\" 2>/dev/null)\" != fixed ]; then echo fixed > \"$0\"; fi; echo x >> generations", "$out", nullptr},
        .inputs = (const char *[]){"trigger", nullptr},
        .outputs = (const char *[]){"generated", nullptr},
        .restat = true,
    };
    smith_step(b, &generate);
    SmithStep copy = {
        .command = (const char *[]){"sh", "-c", "cp \"$0\" \"$1\"; echo x >> copies", "$in", "$out", nullptr},
        .inputs = generate.outputs,
        .outputs = (const char *[]){"copied", nullptr},
    };
    smith_step(b, &copy);
    smith_default(b, copy.outputs);
}

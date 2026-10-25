#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep script = {
        .command = (const char *[]){"sh", "-c",
                                    "if [ \"$1\" = go ]; then\n    echo yes > \"$2\"\nelse\n    echo no > \"$2\"\nfi\necho x >> runs\n",
                                    "sh", "go", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &script);
    smith_default(b, script.outputs);
}

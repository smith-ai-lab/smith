#include <smith/build.h>

static SmithStep object(SmithBuild *b, const char *description) {
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo x >> runs; echo o > \"$0\"", "$out", nullptr},
        .inputs = (const char *[]){"input", nullptr},
        .outputs = (const char *[]){"shared.o", nullptr},
        .description = description,
    };
    smith_step(b, &step);
    return step;
}

void build(SmithBuild *b) {
    bool differ = smith_option(b, "differ", "declare the second step differently");
    SmithStep first = object(b, "make shared.o");
    SmithStep second = object(b, differ ? "other" : "make shared.o");
    SmithStep tool = {.command = (const char *[]){"sh", "-c", "echo x >> tasks", nullptr}, .inputs = first.outputs};
    smith_task(b, "tool", &tool);
    SmithStep again = {.command = (const char *[]){"sh", "-c", "echo x >> tasks", nullptr}, .inputs = second.outputs};
    smith_task(b, "tool", &again);
    smith_alias(b, "group", tool.outputs);
    smith_alias(b, "group", again.outputs);
    smith_default(b, smith_cat(b, first.outputs, second.outputs, nullptr));
}

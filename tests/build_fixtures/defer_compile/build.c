#include <smith/build.h>
#include <stddef.h>

static void more(SmithBuild *b) {
    SmithObject late = {.compiler = "smith-cc", .source = "main.c", .output = "build/late.o"};
    smith_object(b, &late);
    SmithProgram late_program = {.linker = "smith-cc", .output = "build/late", .objects = (const SmithObject *[]){&late, nullptr}};
    smith_program(b, &late_program);
    smith_default(b, late_program.step.outputs);
    smith_build_Lines read_names = smith_lines(b, "list");
    if (read_names.tag != smith_enum_result_const_char_ptr_const_ptr__SmithLinesError_tag_ok) return;
    const char *const *names = read_names.ok;
    for (size_t i = 0; names[i] != nullptr; i++) {
        const char *out = smith_fmt(b, "%s.out", names[i]);
        SmithStep step = {
            .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", names[i], out, nullptr},
            .outputs = (const char *[]){out, nullptr},
        };
        smith_step(b, &step);
        smith_default(b, step.outputs);
    }
}

static int make_list(SmithRun *run) {
    smith_write(run->b, run->outputs[0], "a\nb\n");
    return 0;
}

void build(SmithBuild *b) {
    SmithObject main_object = {.compiler = "smith-cc", .source = "main.c", .output = "build/main.o"};
    smith_object(b, &main_object);
    SmithProgram program = {.linker = "smith-cc", .output = "build/program", .objects = (const SmithObject *[]){&main_object, nullptr}};
    smith_program(b, &program);
    smith_default(b, program.step.outputs);
    SmithStep list = {
        .run = make_list,
        .inputs = (const char *[]){"source", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    smith_defer(b, list.outputs, more);
}

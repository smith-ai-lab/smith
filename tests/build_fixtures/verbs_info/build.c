#include <smith/build.h>

void build(SmithBuild *b) {
    SmithObject main_object = {.compiler = "cc", .source = "main.c", .output = "build/main.o",
                               .flags = (const char *[]){"-DFIRST", nullptr}};
    smith_object(b, &main_object);
    SmithObject again = {.compiler = "cc", .source = "main.c", .output = "build/again.o",
                         .flags = (const char *[]){"-DSECOND", nullptr}};
    smith_object(b, &again);
    SmithStep generate = {.command = (const char *[]){"sh", "-c", "echo 'int made(void) { return 3; }' > $0", "build/made.c", nullptr},
                          .outputs = (const char *[]){"build/made.c", nullptr}};
    smith_step(b, &generate);
    SmithObject made = {.compiler = "cc", .source = "build/made.c", .output = "build/made.o", .flags = nullptr};
    smith_object(b, &made);
    SmithObject helper = {.compiler = "cc", .source = "helper.c", .output = "build/helper.o", .flags = nullptr,
                          .implicit_inputs = generate.outputs};
    smith_object(b, &helper);
    SmithProgram program = {.linker = "cc", .output = "build/program",
                            .objects = (const SmithObject *[]){&main_object, &helper, &made, nullptr}};
    smith_program(b, &program);
    smith_compile_command(b, "cc", "other.c", (const char *[]){"-DOTHER", nullptr});
    SmithTests tests = {.sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)), .flags = (const char *[]){"-I", ".smith/include", nullptr}};
    smith_tests(b, &tests);
    smith_alias(b, "everything", program.step.outputs);
    smith_default(b, smith_list(b, "everything", nullptr));
}

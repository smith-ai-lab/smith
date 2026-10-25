#include <smith/build.h>
#include <stddef.h>

void build(SmithBuild *b) {
    smith_build_Lines read = smith_lines(b, "data");
    const char *word = "missing";
    if (read.tag == smith_enum_result_const_char_ptr_const_ptr__SmithLinesError_tag_ok) {
        word = read.ok[0] == nullptr ? "empty" : "no";
    }
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", word, "result", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}

#include <smith/build.h>
#include <stddef.h>

void build(SmithBuild *b) {
    smith_build_Lines read_lines = smith_lines(b, "data");
    if (read_lines.tag != smith_enum_result_const_char_ptr_const_ptr__SmithLinesError_tag_ok) return;
    const char *const *lines = read_lines.ok;
    const char *text = smith_fmt(b, "%zu", (size_t)0);
    size_t count = 0;
    while (lines[count] != nullptr)
        count++;
    text = smith_fmt(b, "%zu[%s][%s][%s]", count, lines[0], lines[1], lines[2]);
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0\" > \"$1\"", text, "result", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}

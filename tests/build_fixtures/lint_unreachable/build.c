#include <smith/build.h>

// A step that fails the way a fetch the network does not give fails: it leaves the url in .smith/unreachable, as
// build_package_path does. a.c waits for its output, whose name holds "tree-sitter" as the parser's does; c.c waits for
// nothing. `smith lint` skips a.c by name and lints c.c (tests/lint_changed_test.smith).
void build(SmithBuild *b) {
    SmithStep fetch = {.command = (const char *[]){"sh", "-c",
                                                   "mkdir -p .smith && echo https://example.invalid/tree-sitter.gz > "
                                                   ".smith/unreachable && exit 1",
                                                   nullptr},
                       .outputs = (const char *[]){"build/tree-sitter-parser.c", nullptr},
                       .description = "fetch the parser"};
    smith_step(b, &fetch);
    SmithObject embedded = {.compiler = "smith-cc", .source = "a.c", .output = "build/a.o",
                            .implicit_inputs = fetch.outputs};
    SmithObject plain = {.compiler = "smith-cc", .source = "c.c", .output = "build/c.o"};
    smith_object(b, &embedded);
    smith_object(b, &plain);
    smith_default(b, smith_list(b, "build/c.o", nullptr));
}

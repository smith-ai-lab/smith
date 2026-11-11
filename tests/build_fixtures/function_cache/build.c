// A function step the step cache serves (#3079): `shout` writes its input in capitals, twice, to two outputs. It also
// appends a line to `runs`, a file it does not declare, so a test can count how often the function ran. `-Dhash=0`
// leaves .content_hash out, `-Dsay=1` makes the function print, `-Dalways=1` makes the step .always (an error).
#include <ctype.h>
#include <smith/build.h>
#include <stdio.h>
#include <string.h>

static int shout(SmithRun *run) {
    FILE *runs = fopen("runs", "a");
    if (runs == nullptr) return 1;
    fputs("ran\n", runs);
    fclose(runs);
    char text[256] = {};
    FILE *in = fopen(run->inputs[0], "r");
    if (in == nullptr) return 1;
    size_t size = fread(text, 1, sizeof text - 1, in);
    fclose(in);
    for (size_t i = 0; i < size; i++) text[i] = (char)toupper((unsigned char)text[i]);
    for (size_t k = 0; k < 2; k++) {
        FILE *out = fopen(run->outputs[k], "w");
        if (out == nullptr) return 1;
        fprintf(out, "%s%s%s", run->args[0], k == 0 ? "" : "again: ", text);
        fclose(out);
    }
    if (strcmp(run->args[1], "1") == 0) puts("shouting");
    return 0;
}

void build(SmithBuild *b) {
    SmithStep step = {
        .run = shout,
        .args = smith_list(b, smith_option_value(b, "prefix", "", "what each output starts with"),
                           smith_option_value(b, "say", "0", "1: the function prints"), nullptr),
        .inputs = smith_list(b, "word.txt", nullptr),
        .outputs = smith_list(b, "build/loud.txt", "build/louder.txt", nullptr),
        .description = "shout word.txt",
        .always = strcmp(smith_option_value(b, "always", "0", "1: the step is .always"), "1") == 0,
        .content_hash = strcmp(smith_option_value(b, "hash", "1", "0: no .content_hash"), "0") != 0,
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}

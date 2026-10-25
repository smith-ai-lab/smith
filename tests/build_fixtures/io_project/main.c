#include <smith/io.h>

#include <stdio.h>

// Takes the two capabilities it uses and nothing else.
static size_t count_words(SmithFiles *files, SmithArena *arena, const char *path) {
    SmithRead text = smith_files_read(files, arena, path);
    size_t words = 0;
    bool inside = false;
    for (size_t i = 0; i < text.data.count[0]; i++) {
        bool space = text.data.data[i] == ' ' || text.data.data[i] == '\n';
        if (!space && !inside) words += 1;
        inside = !space;
    }
    return words;
}

int main(int argc, char **argv) {
    SmithIo io = smith_io_make(argc, argv);
    SmithError error = smith_files_write(
        &io.files, "words.txt",
        (struct smith_array_const_u8_n){.data = (const uint8_t *)"one two  three\nfour", .count = {19}}, SmithWrite_Replace);
    printf("write %d, words %zu, seed %d\n", error.code, count_words(&io.files, &io.arena, "words.txt"), io.seed != 0);
    return 0;
}

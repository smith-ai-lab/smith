#include <smith/test.h>

#include <stdio.h>
#include <string.h>

void test_reads_input(void) {
    FILE *file = fopen("build/data.txt", "r");
    smith_check(file != nullptr);
    char text[16] = "";
    smith_check(fgets(text, sizeof text, file) != nullptr);
    fclose(file);
    smith_test_note("data %s", text);
    smith_check_equal_i32(strcmp(text, "one\n"), 0);
}

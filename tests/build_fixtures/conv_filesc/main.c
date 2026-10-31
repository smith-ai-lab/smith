#include <smith/files.h>

#include <stdio.h>

int main(void) {
    smith_files_InfoResult info = smith_fs_info((struct smith_array_const_char_n){".", {1}});
    printf("found %d\n", info.tag == smith_enum_result_SmithFilesInfo__SmithFilesError_tag_ok);
    return 0;
}

#include <smith/files.h>
#include <smith/test.h>

void test_files_reads_a_folder(void) {
    smith_files_InfoResult info = smith_fs_info((struct smith_array_const_char_n){".", {1}});
    smith_check(info.tag == smith_enum_result_SmithFilesInfo__SmithFilesError_tag_ok);
}

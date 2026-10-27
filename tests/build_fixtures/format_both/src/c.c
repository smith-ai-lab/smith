#include <smith/format.h>

unsigned long long from_c(void) {
    smith_format_U64Result result = smith_format_parse_u64(smith_format_string("7"));
    return result.tag == smith_enum_result_u64__SmithFormatError_tag_ok ? result.ok : 0;
}

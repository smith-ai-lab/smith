#include <smith/text.h>

unsigned long long from_c(void) {
    unsigned long long value = 0;
    return smith_text_parse_u64(smith_text_make("7"), &value) ? value : 0;
}

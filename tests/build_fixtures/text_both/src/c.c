#include <smith/text.h>
#include <stdint.h>

uint64_t from_c(void) {
    uint64_t value = 0;
    return smith_text_parse_u64(smith_text_make("7"), &value) ? value : 0;
}

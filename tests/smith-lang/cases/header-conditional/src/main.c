#include <stdio.h>

#include "cfg/mode.h"

int main(void) {
    cfg_mode_Cell cell = {.value = 9};
    printf("%zu %u %u %u\n", sizeof cell.value, cfg_mode_width(0), cfg_mode_always(0), cfg_mode_plain(0));
    return 0;
}

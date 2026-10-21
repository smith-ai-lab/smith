#include <stdio.h>

#include "cfg/mode.h"

int main(void) {
    cfg_mode_Cell cell = {.value = 9};
    printf("%zu %u %u %u\n", sizeof cell.value, cfg_mode_width(), cfg_mode_always(), cfg_mode_plain());
    return 0;
}

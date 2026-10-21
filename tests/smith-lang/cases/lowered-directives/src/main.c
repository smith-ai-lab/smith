#include <stdio.h>

#include "demo/dir.h"

int main(void) {
    printf("%u %u %zu\n", demo_dir_width(1), demo_dir_pair(1, 2), demo_dir_name_length());
    return 0;
}

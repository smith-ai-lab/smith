#include "ops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    int a = atoi(argv[2]);
    int b = atoi(argv[3]);
    if (strcmp(argv[1], "add") == 0) {
        printf("%d\n", ops_add(a, b));
        return 0;
    }
    if (strcmp(argv[1], "mul") == 0) {
        printf("%d\n", ops_mul(a, b));
        return 0;
    }
    return 2;
}

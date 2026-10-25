#include <smith/contract.h>

static int positive(int value) {
    smith_requires(value > 0);
    return value;
}

int main(int argc, char **argv) {
    (void)argv;
    return positive(argc - 1);
}

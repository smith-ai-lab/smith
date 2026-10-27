#include <stdio.h>

int late_value(void);

int main(int argc, char **argv) {
    (void)argv;
    if (argc > 1) return late_value();
    puts("main");
    return 0;
}

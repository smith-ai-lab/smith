#include <stdbool.h>
#include <stddef.h>

__attribute__((noreturn)) void halt();
__attribute__((unused, warn_unused_result)) static int compute(void) { return 0; }
_Noreturn void quit(void);

int f(const char *p) {
    int values[3] = {0};
    return (p == NULL) + values[0];
}

_Static_assert(sizeof(int) == 4, "int");
_Alignas(16) static char buffer[16];
static const unsigned long alignment = _Alignof(int);

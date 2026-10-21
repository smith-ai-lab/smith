// A function with a variable argument list in the program does not stop the others from being proved (issue #41): it
// is only an error where a function being proved calls it.
#include <smith/contract.h>
#include <stdarg.h>
#include <stdint.h>

static int32_t sum(int32_t count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int32_t total = 0;
    for (int32_t i = 0; i < count && i < 4; i++)
        total += va_arg(arguments, int32_t) & 15;
    va_end(arguments);
    return total;
}

int32_t twice(int32_t x) {
    smith_requires(x >= 0 && x < 1000);
    int32_t result = x * 2;
    smith_ensures(result == x + x);
    return result;
}

int32_t uses_sum(void) {
    int32_t result = sum(0);
    smith_ensures(result == 0);
    return result;
}

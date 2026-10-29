#include <stdio.h>

// A printf with a float conversion keeps libc's dtoa cache: `globals`. A literal without one keeps the table's bits,
// and a format that is not a literal is judged as if it had one.
[[smith::pure]] int plain(char *buffer, int n) {
    return snprintf(buffer, 32, "%d %5.2s %%f", n, "xyz");
}

[[smith::pure]] int floaty(char *buffer, double x) {
    return snprintf(buffer, 32, "%8.3f", x);
}

[[smith::pure]] int wide(char *buffer, long double x) {
    return snprintf(buffer, 32, "%-+10.4Lg", x);
}

[[smith::pure]] int dynamic(char *buffer, const char *format, int n) {
    return snprintf(buffer, 32, format, n);
}

[[smith::effects(console)]] void shown(double x) {
    printf("%e\n", x);
}

// macOS's <stdio.h> writes snprintf as its fortified builtin: the same function, with the format two arguments later.
[[smith::pure]] int fortified(char *buffer, double x) {
    return __builtin___snprintf_chk(buffer, 32, 0, __builtin_object_size(buffer, 0), "%.1f", x);
}

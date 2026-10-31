// Brackets in C that a lambda's `[` could be taken for: array sizes left empty, designators, subscripts after a name,
// a call or another subscript, and a parameter of array type. A lambda's `[]` follows `=`, `(`, `,` or `return`, which
// none of these do; each prints what it prints in a .c file.
#include <stdio.h>

static int first(const int values[], int count) {
    return count > 0 ? values[0] : -1;
}

static const int *pointer(const int *values) {
    return values;
}

int main(void) {
    int sized[] = {4, 5, 6};
    int designated[4] = {[2] = 7, [0] = 8};
    int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
    const char *names[] = {"a", "b"};
    printf("%d %d %d\n", first(sized, 3), designated[0] + designated[2], pointer(sized)[1]);
    printf("%d %d %zu %s\n", matrix[1][2], (matrix[0])[1], sizeof sized / sizeof sized[0], names[1]);
    return 0;
}

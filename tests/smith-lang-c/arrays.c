// Array parameters mean what they mean in C: a pointer, whatever the brackets say.
#include <stddef.h>
#include <stdio.h>

static int fixed(int a[4]) {
    return a[0] + a[3];
}
static int vla(int n, int a[n]) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += a[i];
    return s;
}
static int at_least(int a[static 3]) {
    return a[0] + a[1] + a[2];
}
static int first_of(int *const a[const 2]) {
    return **a;
}
static int star(int n, int a[*]);
static int star(int n, int a[n]) {
    return n ? a[n - 1] : -1;
}
static int matrix(int n, int m[][n]) {
    return m[1][n - 1];
}
static size_t of_parameter(int a[10]) {
    return sizeof a;
}
static size_t of_element(int a[10]) {
    return sizeof a[0];
}
static size_t of_pointee(char (*p)[7]) {
    return sizeof *p;
}

int main(void) {
    int four[4] = {1, 2, 3, 4};
    int ten[10] = {0};
    int *twice[2] = {four, four};
    int rows[2][3] = {{1, 2, 3}, {4, 5, 6}};
    char seven[7];
    printf("%d %d %d %d\n", fixed(four), vla(4, four), at_least(four), first_of(twice));
    printf("%d %d\n", star(4, four), matrix(3, rows));
    printf("%zu %zu %zu %zu\n", of_parameter(ten), of_element(ten), sizeof ten, sizeof(int[10]));
    printf("%zu\n", of_pointee(&seven));
    return 0;
}

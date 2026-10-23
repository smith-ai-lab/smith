// Words the language uses elsewhere are ordinary names here: a file with no module line keeps their C meaning.
#include <stdio.h>

typedef int import;
import imported = 3;
static int export = 4;
typedef int module;
module modular = 5;

struct c_holder {
    int x;
};

static int countof(int x) {
    return x * 2;
}
static int type_name(int type) {
    return type;
}
static void same_type(int a, typeof(a) b, int *out) {
    *out = a + b;
}
static int rest(int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += i;
    return s;
}

int main(void) {
    struct c_holder c = {5};
    int io = c.x, u8 = 2, constexpr_ = 1, out = 0;
    same_type(3, 4, &out);
    printf("%d %d %d\n", imported, export, modular);
    printf("%d %d %d %d\n", countof(4), type_name(9), out, rest(4));
    printf("%d %d %s\n", io, u8, "x::y");
    if (constexpr_) printf("%d\n", countof(1));
    return 0;
}

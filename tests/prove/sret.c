// A struct too big for registers is returned through an `sret` pointer, which Alive2 cannot translate; the prover makes it a
// dereferenceable pointer, so the function and its callers are proved (issue #74).
#include <smith/contract.h>

typedef struct {
    int a, b, c, d, e, f;
} Wide;

Wide make_wide(int a, int b) {
    smith_requires(a >= 0 && a < 1000 && b >= 0 && b < 1000);
    Wide wide = {a, b, a + b, a * 2, 0, 7};
    smith_ensures(wide.c == a + b && wide.d == a * 2 && wide.a == a && wide.f == 7);
    return wide;
}

int use_wide(int a) {
    smith_requires(a >= 0 && a < 1000);
    Wide wide = make_wide(a, 1);
    smith_ensures(wide.c == a + 1);
    return wide.c;
}

Wide wrong_wide(int a) {
    Wide wide = {a, 0, 0, 0, 0, 0};
    smith_ensures(wide.b == 1);
    return wide;
}

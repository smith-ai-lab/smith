// #57's caller: it passes one object as an int32_t * and as a uint32_t *, and asserts what signed_unsigned's contract
// says. The claim is false at run time, so smith prove must fail the program: signed_unsigned's own proof fails, since
// its contract is the lie the caller relies on (the caller alone is "ok", the program is not).
// unsound: signed_unsigned ensures failed
// unsound-program: caller_of_signed_unsigned ensures failed
#include <smith/contract.h>

#include <stdint.h>
#include <string.h>

void signed_unsigned(int32_t *a, uint32_t *b) {
    smith_requires(*a == 1);
    *b = 2;
    smith_ensures(*a == 1);
}

void caller_of_signed_unsigned(int32_t *p) {
    smith_requires(*p == 1);
    signed_unsigned(p, (uint32_t *)p);
    smith_ensures(*p == 1);
}

int main(int argc, char **argv) {
    if (argc < 2) return 0;
    int32_t x = 1;
    if (strcmp(argv[1], "signed_unsigned") == 0) signed_unsigned(&x, (uint32_t *)&x);
    if (strcmp(argv[1], "caller_of_signed_unsigned") == 0) caller_of_signed_unsigned(&x);
    return 0;
}

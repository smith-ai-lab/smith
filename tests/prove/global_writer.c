// A callee that writes a global may write memory its caller did not give it: the note names the store LLVM found (#155).
#include <smith/contract.h>

int counter;

int bump(int x) {
    smith_ensures(true);
    counter += x;
    return x;
}

int use(int x) {
    smith_ensures(true);
    int y = bump(x);
    smith_ensures(y == x + 1);
    return y;
}

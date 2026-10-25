#include <smith/contract.h>

#include <stdio.h>

static int wrong(int x) {
    int result = x + 1;
    smith_ensures(result == x);
    return result;
}

int main(void) {
    printf("%d\n", wrong(1));
    return 0;
}

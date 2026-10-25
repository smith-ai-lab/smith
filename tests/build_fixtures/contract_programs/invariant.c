#include <smith/contract.h>

int main(void) {
    int total = 0;
    for (int i = 0; i < 4; i++) {
        smith_invariant(total == i * 2);
        total += 2;
        if (i == 1) total++;
    }
    return total;
}

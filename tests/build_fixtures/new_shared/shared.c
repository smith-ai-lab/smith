#include <smith/contract.h>

#include "shared.h"

int shared_double(int x) {
    smith_requires(0 <= x && x <= 1000);
    int result = 2 * x;
    smith_ensures(result <= 2000);
    return result;
}

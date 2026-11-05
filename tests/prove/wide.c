// A 128-bit integer (`u128` in smith's own syntax, `unsigned __int128` in C, LLVM `i128`): its contracts are proved.
#include <smith/contract.h>

unsigned __int128 next(unsigned __int128 x) {
    smith_requires(x < 100);
    unsigned __int128 result = x + 1;
    smith_ensures(result <= 100);
    return result;
}

unsigned __int128 too_sure(unsigned __int128 x) {
    smith_requires(x < 100);
    unsigned __int128 result = x + 1;
    smith_ensures(result <= 99);
    return result;
}

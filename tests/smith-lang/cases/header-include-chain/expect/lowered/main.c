#include "inc/chain.h"
#include <stdio.h>

__INT32_TYPE__ main(void) {
printf("%u %.1f\n", inc_chain_length("smith"), inc_chain_root(16.0));
return 0;
}

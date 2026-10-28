#include "bad.h"

int main(void) {
__INT32_TYPE__ a =__extension__({extern __INT32_TYPE__ bad_no_step__(void)__asm__("_bad::no_step()");bad_no_step__();});
__INT32_TYPE__ b =__extension__({extern __INT32_TYPE__ bad_no_variable__(void)__asm__("_bad::no_variable()");bad_no_variable__();});
__INT32_TYPE__ c =smith_call_failed;
__INT32_TYPE__ f =smith_call_failed;
return a + b + c + f;
}
extern __INT32_TYPE__ bad_no_step__(void)__asm__("_bad::no_step()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ bad_no_step__(void){{}return 0;}
extern __INT32_TYPE__ bad_no_variable__(void)__asm__("_bad::no_variable()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ bad_no_variable__(void){return 0;}

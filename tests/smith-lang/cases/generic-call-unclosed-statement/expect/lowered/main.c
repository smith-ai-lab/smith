#include "num.h"

int main() {
int x =smith_call_failed;
int y =__extension__({extern __INT32_TYPE__ num_id__i32(__INT32_TYPE__ x)__asm__("_num::id(__INT32_TYPE__)");num_id__i32(2);});
return x + y;
}
extern __INT32_TYPE__ num_id__i32(__INT32_TYPE__ x)__asm__("_num::id(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ num_id__i32(__INT32_TYPE__ x){return x;}

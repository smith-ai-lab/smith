#include "num.h"

int main() {
return __extension__({extern __INT32_TYPE__ num_sign__I32(__INT32_TYPE__ value)__asm__("_num::sign(__INT32_TYPE__)");num_sign__I32(3);}) +__extension__({extern __INT32_TYPE__ num_many__I32(void)__asm__("_num::many(__INT32_TYPE__)");num_many__I32();});
}
extern __INT32_TYPE__ num_sign__I32(__INT32_TYPE__ value)__asm__("_num::sign(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ num_sign__I32(__INT32_TYPE__ value){}
extern __INT32_TYPE__ num_many__I32(void)__asm__("_num::many(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ num_many__I32(void){return 2;}

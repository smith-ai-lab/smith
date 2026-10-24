#include "nouns.h"
#include <stdio.h>
typedef struct {
int a;
} Pair;
typedef enum { red, green } Color;
typedef int Three[3];
typedef char *Text;
constexpr type kinds[10] = {__INT32_TYPE__, __UINT8_TYPE__, double, Pair, bool, Text, Color, Three, __INT32_TYPE__, __SIZE_TYPE__};
static type to_slice(type T) {
return T[:];
}
constexpr type sliced = to_slice(__INT32_TYPE__);
static_assert(integer(kinds[0]) && integer(kinds[1]) && !integer(kinds[2]) && !integer(kinds[4]) && !integer(kinds[6]));
static_assert(floating(kinds[2]) && !floating(kinds[0]));
static_assert(signed_integer(kinds[0]) && !signed_integer(kinds[1]) && !signed_integer(kinds[2]));
static_assert(unsigned_integer(kinds[1]) && unsigned_integer(kinds[9]) && !unsigned_integer(kinds[0]) &&
!unsigned_integer(kinds[2]));
static_assert(arithmetic(kinds[0]) && arithmetic(kinds[2]) && !arithmetic(kinds[3]) && !arithmetic(kinds[5]));
static_assert(boolean(kinds[4]) && !boolean(kinds[0]));
static_assert(pointer(kinds[5]) && !pointer(kinds[0]));
static_assert(enumeration(kinds[6]) && !enumeration(kinds[0]));
static_assert(structure(kinds[3]) && !structure(kinds[0]));
static_assert(array(kinds[7]) && !array(kinds[0]));
static_assert(slice(sliced) && !slice(kinds[5]));
static_assert(integer(kinds[0]) == is_integer(kinds[0]) && arithmetic(kinds[2]) == (is_integer(kinds[2]) || is_float(kinds[2])));
int main(void) {
printf("%d %d %d %d\n", (int)integer(kinds[0]), (int)arithmetic(kinds[2]), (int)unsigned_integer(kinds[2]),
(int)signed_integer(kinds[2]));
__INT32_TYPE__ a = 1;
__UINT8_TYPE__ b = 2;
double c = 3;
Pair p = {4};
printf("%d %d %d %d %d\n",__extension__({extern __INT32_TYPE__ nouns_kind_of__i32(__INT32_TYPE__ value)__asm__("_nouns::kind_of(__INT32_TYPE__)");nouns_kind_of__i32(a);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__u8(__UINT8_TYPE__ value)__asm__("_nouns::kind_of(__UINT8_TYPE__)");nouns_kind_of__u8(b);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__f64(double value)__asm__("_nouns::kind_of(double)");nouns_kind_of__f64(c);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__char_P(char*value)__asm__("_nouns::kind_of(char *)");nouns_kind_of__char_P(nullptr);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__Pair(Pair value)__asm__("_nouns::kind_of(Pair)");nouns_kind_of__Pair(p);}));
return 0;
}
extern __INT32_TYPE__ nouns_kind_of__i32(__INT32_TYPE__ value)__asm__("_nouns::kind_of(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__i32(__INT32_TYPE__ value){}
extern __INT32_TYPE__ nouns_kind_of__u8(__UINT8_TYPE__ value)__asm__("_nouns::kind_of(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__u8(__UINT8_TYPE__ value){}
extern __INT32_TYPE__ nouns_kind_of__f64(double value)__asm__("_nouns::kind_of(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__f64(double value){}
extern __INT32_TYPE__ nouns_kind_of__char_P(char*value)__asm__("_nouns::kind_of(char *)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__char_P(char*value){}
extern __INT32_TYPE__ nouns_kind_of__Pair(Pair value)__asm__("_nouns::kind_of(Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__Pair(Pair value){}

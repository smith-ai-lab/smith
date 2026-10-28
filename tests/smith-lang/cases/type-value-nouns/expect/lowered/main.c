#include "nouns.h"
#include <stdio.h>
typedef struct {
int a;
} Pair;
typedef enum { red, green } Color;
typedef int Three[3];
typedef char *Text;
constexpr Type kinds[10] = {__INT32_TYPE__, __UINT8_TYPE__, double, Pair, bool, Text, Color, Three, __INT32_TYPE__, __SIZE_TYPE__};
static Type ToSlice(Type T) {
return T[:];
}
constexpr Type Sliced = ToSlice(__INT32_TYPE__);
static_assert(Int(kinds[0]) && Int(kinds[1]) && !Int(kinds[2]) && !Int(kinds[4]) && !Int(kinds[6]));
static_assert(Float(kinds[2]) && !Float(kinds[0]));
static_assert(Signed(kinds[0]) && !Signed(kinds[1]) && !Signed(kinds[2]));
static_assert(Unsigned(kinds[1]) && Unsigned(kinds[9]) && !Unsigned(kinds[0]) &&
!Unsigned(kinds[2]));
static_assert(Num(kinds[0]) && Num(kinds[2]) && !Num(kinds[3]) && !Num(kinds[5]));
static_assert((bool == (kinds[4])) && !(bool == (kinds[0])));
static_assert(Pointer(kinds[5]) && !Pointer(kinds[0]));
static_assert(Enum(kinds[6]) && !Enum(kinds[0]));
static_assert(Struct(kinds[3]) && !Struct(kinds[0]));
static_assert(Array(kinds[7]) && !Array(kinds[0]));
static_assert(Slice(Sliced) && !Slice(kinds[5]));
static_assert(Int(kinds[0]) == Int(kinds[0]) && Num(kinds[2]) == (Int(kinds[2]) || Float(kinds[2])));
int main() {
printf("%d %d %d %d\n", (int)Int(kinds[0]), (int)Num(kinds[2]), (int)Unsigned(kinds[2]),
(int)Signed(kinds[2]));
__INT32_TYPE__ a = 1;
__UINT8_TYPE__ b = 2;
double c = 3;
Pair p = {4};
printf("%d %d %d %d %d\n",__extension__({extern __INT32_TYPE__ nouns_kind_of__I32(__INT32_TYPE__ value)__asm__("_nouns::kind_of(__INT32_TYPE__)");nouns_kind_of__I32(a);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__U8(__UINT8_TYPE__ value)__asm__("_nouns::kind_of(__UINT8_TYPE__)");nouns_kind_of__U8(b);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__F64(double value)__asm__("_nouns::kind_of(double)");nouns_kind_of__F64(c);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__char_P(char*value)__asm__("_nouns::kind_of(char *)");nouns_kind_of__char_P(nullptr);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__Pair(Pair value)__asm__("_nouns::kind_of(Pair)");nouns_kind_of__Pair(p);}));
return 0;
}
extern __INT32_TYPE__ nouns_kind_of__I32(__INT32_TYPE__ value)__asm__("_nouns::kind_of(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__I32(__INT32_TYPE__ value){}
extern __INT32_TYPE__ nouns_kind_of__U8(__UINT8_TYPE__ value)__asm__("_nouns::kind_of(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__U8(__UINT8_TYPE__ value){}
extern __INT32_TYPE__ nouns_kind_of__F64(double value)__asm__("_nouns::kind_of(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__F64(double value){}
extern __INT32_TYPE__ nouns_kind_of__char_P(char*value)__asm__("_nouns::kind_of(char *)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__char_P(char*value){}
extern __INT32_TYPE__ nouns_kind_of__Pair(Pair value)__asm__("_nouns::kind_of(Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__Pair(Pair value){}

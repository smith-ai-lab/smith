#include "nouns.h"
#include <stdio.h>

struct main_Pair {
__INT32_TYPE__ a;
};
typedef struct main_Pair main_Pair;
enum main_Color { main_red, main_green };
typedef enum main_Color main_Color;
typedef __INT32_TYPE__ main_Three[3];
typedef char *main_Text;
static constexpr type main_kinds[10] = {__INT32_TYPE__, __UINT8_TYPE__, double, main_Pair, bool, main_Text, main_Color, main_Three, __INT32_TYPE__, __SIZE_TYPE__}; [ [ clang :: unavailable ( "main::to_slice is a function over `type`: it exists only at compile time" ) ] ] void main_to_slice ( void ) ;
static constexpr type main_sliced = main_to_slice(__INT32_TYPE__);
static_assert(smith_concepts_Int(main_kinds[0]) && smith_concepts_Int(main_kinds[1]) && !smith_concepts_Int(main_kinds[2]) && !smith_concepts_Int(main_kinds[4]) && !smith_concepts_Int(main_kinds[6]));
static_assert(smith_concepts_Float(main_kinds[2]) && !smith_concepts_Float(main_kinds[0]));
static_assert(smith_concepts_Signed(main_kinds[0]) && !smith_concepts_Signed(main_kinds[1]) && !smith_concepts_Signed(main_kinds[2]));
static_assert(smith_concepts_Unsigned(main_kinds[1]) && smith_concepts_Unsigned(main_kinds[9]) && !smith_concepts_Unsigned(main_kinds[0]) && !smith_concepts_Unsigned(main_kinds[2]));
static_assert(smith_concepts_Num(main_kinds[0]) && smith_concepts_Num(main_kinds[2]) && !smith_concepts_Num(main_kinds[3]) && !smith_concepts_Num(main_kinds[5]));
static_assert(main_kinds[4] == bool && main_kinds[0] != bool);
static_assert(smith_concepts_Pointer(main_kinds[5]) && !smith_concepts_Pointer(main_kinds[0]));
static_assert(smith_concepts_Enum(main_kinds[6]) && !smith_concepts_Enum(main_kinds[0]));
static_assert(smith_concepts_Struct(main_kinds[3]) && !smith_concepts_Struct(main_kinds[0]));
static_assert(smith_concepts_Array(main_kinds[7]) && !smith_concepts_Array(main_kinds[0]));
static_assert(smith_concepts_Slice(main_sliced) && !smith_concepts_Slice(main_kinds[5]));
static_assert(smith_concepts_Int(main_kinds[0]) == is_integer(main_kinds[0]) && smith_concepts_Num(main_kinds[2]) == (is_integer(main_kinds[2]) || is_float(main_kinds[2])));
__INT32_TYPE__ main(void) {
printf("%d %d %d %d\n", (__INT32_TYPE__)smith_concepts_Int(main_kinds[0]), (__INT32_TYPE__)smith_concepts_Num(main_kinds[2]), (__INT32_TYPE__)smith_concepts_Unsigned(main_kinds[2]),
(__INT32_TYPE__)smith_concepts_Signed(main_kinds[2]));
__INT32_TYPE__ a = 1;
__UINT8_TYPE__ b = 2;
double c = 3;
main_Pair p = {4};
printf("%d %d %d %d %d\n",__extension__({extern __INT32_TYPE__ nouns_kind_of__i32(__INT32_TYPE__ value)__asm__("_nouns::kind_of(__INT32_TYPE__)");nouns_kind_of__i32(a);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__u8(__UINT8_TYPE__ value)__asm__("_nouns::kind_of(__UINT8_TYPE__)");nouns_kind_of__u8(b);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__f64(double value)__asm__("_nouns::kind_of(double)");nouns_kind_of__f64(c);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__char_P(char*value)__asm__("_nouns::kind_of(char *)");nouns_kind_of__char_P(nullptr);}),__extension__({extern __INT32_TYPE__ nouns_kind_of__Pair(main_Pair value)__asm__("_nouns::kind_of(main_Pair)");nouns_kind_of__Pair(p);}));
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
extern __INT32_TYPE__ nouns_kind_of__Pair(main_Pair value)__asm__("_nouns::kind_of(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ nouns_kind_of__Pair(main_Pair value){}

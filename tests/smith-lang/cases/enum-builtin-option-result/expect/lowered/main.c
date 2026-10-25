#include <stdio.h>
#include "finder.h"
#include "parser.h"
struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_option_usize main_Index;struct smith_enum_result_f64__i32 {enum smith_enum_result_f64__i32_tag {smith_enum_result_f64__i32_tag_ok, smith_enum_result_f64__i32_tag_error } tag; union { double ok; __INT32_TYPE__ error; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_result_f64__i32 main_Quotient; [ [ clang :: unavailable ( "main::first is a generic function: call it from a .smith module" ) ] ] void main_first ( void ) ;
static __SIZE_TYPE__ main_or_else(main_Index at, __SIZE_TYPE__ otherwise) {
if (at == .some) return at.some;
return otherwise;
}
__INT32_TYPE__ main(void) {
__SIZE_TYPE__ xs[2] = {7, 8};
main_Index a = finder_find("key=value", '=');
main_Index b = parser_position("a,b", ',');
main_Index c =__extension__({extern struct smith_enum_option_usize main_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_main::first(__SIZE_TYPE__)");main_first__usize(xs,2);});
main_Quotient good = parser_divide(6, 3);
main_Quotient bad = parser_divide(1, 0);
printf("%zu %zu %zu\n", main_or_else(a, 99), main_or_else(b, 99), main_or_else(c, 99));
switch (good) {
case .ok: printf("ok %g\n", good.ok); break;
case .error: printf("error %d\n", good.error); break;
}
switch (bad) {
case .ok: printf("ok\n"); break;
case .error: printf("error %d\n", bad.error); break;
}
return 0;
}
extern struct smith_enum_option_usize main_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_main::first(__SIZE_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_option_usize main_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n){if(n==0)return.none;return{.some=xs[0]};}

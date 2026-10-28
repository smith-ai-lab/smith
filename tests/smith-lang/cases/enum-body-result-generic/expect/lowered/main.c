#include <stdio.h>

static __SIZE_TYPE__ main_divide(__SIZE_TYPE__ a, __SIZE_TYPE__ b) { struct smith_enum_result_usize__i32 {enum smith_enum_result_usize__i32_tag {smith_enum_result_usize__i32_tag_error, smith_enum_result_usize__i32_tag_ok } tag; union { __INT32_TYPE__ error; __SIZE_TYPE__ ok; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_result_usize__i32 r = {.error = 7};
if (b != 0) { r = {.ok = a / b}; } struct smith_enum_maybe_i32 {enum smith_enum_maybe_i32_tag {smith_enum_maybe_i32_tag_some, smith_enum_maybe_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_maybe_i32 m = {.some = 5};
if (r == .ok && m == .some) return r.ok + (__SIZE_TYPE__)m.some;
return 0;
} [ [ clang :: unavailable ( "main::via_generic is a generic function: call it from a .smith module" ) ] ] void main_via_generic ( void ) ;
__INT32_TYPE__ main(void) {
printf("%zu %zu\n", main_divide(8, 2),__extension__({extern __SIZE_TYPE__ main_via_generic__i32(__INT32_TYPE__ v)__asm__("_main::via_generic(__INT32_TYPE__)");main_via_generic__i32(9);}));
return 0;
}
extern __SIZE_TYPE__ main_via_generic__i32(__INT32_TYPE__ v)__asm__("_main::via_generic(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ main_via_generic__i32(__INT32_TYPE__ v){__SIZE_TYPE__ k=(__SIZE_TYPE__)v;{struct smith_enum_option_usize{enum smith_enum_option_usize_tag{smith_enum_option_usize_tag_none,smith_enum_option_usize_tag_some}tag;union{__SIZE_TYPE__ some;};}__attribute__((annotate("smith::enum")));struct smith_enum_option_usize o={.some=k};struct smith_enum_result_usize__i32{enum smith_enum_result_usize__i32_tag{smith_enum_result_usize__i32_tag_error,smith_enum_result_usize__i32_tag_ok}tag;union{__INT32_TYPE__ error;__SIZE_TYPE__ ok;};}__attribute__((annotate("smith::enum")));struct smith_enum_result_usize__i32 r={.ok=k};return k+sizeof(o)*0+sizeof(r)*0;}}

#include <stdio.h>
struct smith_enum_result_usize__i32 {enum smith_enum_result_usize__i32_tag {smith_enum_result_usize__i32_tag_ok, smith_enum_result_usize__i32_tag_error } tag; union { __SIZE_TYPE__ ok; __INT32_TYPE__ error; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_result_usize__i32 main_Wide;struct smith_enum_result_f64__i32 {enum smith_enum_result_f64__i32_tag {smith_enum_result_f64__i32_tag_ok, smith_enum_result_f64__i32_tag_error } tag; union { double ok; __INT32_TYPE__ error; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_result_f64__i32 main_Real;struct smith_enum_option_u8 {enum smith_enum_option_u8_tag {smith_enum_option_u8_tag_some, smith_enum_option_u8_tag_none } tag; union { __UINT8_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_option_u8 main_Small;struct smith_enum_option_f64 {enum smith_enum_option_f64_tag {smith_enum_option_f64_tag_some, smith_enum_option_f64_tag_none } tag; union { double some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_option_f64 main_Float;struct smith_enum_result_i32__i32 {enum smith_enum_result_i32__i32_tag {smith_enum_result_i32__i32_tag_ok, smith_enum_result_i32__i32_tag_error } tag; union { __INT32_TYPE__ ok; __INT32_TYPE__ error; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_result_i32__i32 main_step(__INT32_TYPE__ n) {
if (n < 0) return {.error = n};
return {.ok = n};
}struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_some, smith_enum_option_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_option_i32 main_pick(__INT32_TYPE__ n) {
if (n < 0) return .none;
return {.some = n};
} [ [ clang :: unavailable ( "main::twice is a generic function: call it from a .smith module" ) ] ] void main_twice ( void ) ; [ [ clang :: unavailable ( "main::maybe_twice is a generic function: call it from a .smith module" ) ] ] void main_maybe_twice ( void ) ;
static void main_show_wide(main_Wide r) {
switch (r) {
case .ok: printf("ok %zu\n", r.ok); break;
case .error: printf("error %d\n", r.error); break;
}
}
static void main_show_real(main_Real r) {
switch (r) {
case .ok: printf("ok %g\n", r.ok); break;
case .error: printf("error %d\n", r.error); break;
}
}
static void main_show_small(main_Small o) {
switch (o) {
case .some: printf("some %d\n", o.some); break;
case .none: printf("none\n"); break;
}
}
static void main_show_float(main_Float o) {
switch (o) {
case .some: printf("some %g\n", o.some); break;
case .none: printf("none\n"); break;
}
}
__INT32_TYPE__ main(void) {
main_show_wide(__extension__({extern struct smith_enum_result_usize__i32 main_twice__usize(__SIZE_TYPE__ x,__INT32_TYPE__ n)__asm__("_main::twice(__SIZE_TYPE__)");main_twice__usize(10,5);}));
main_show_wide(__extension__({extern struct smith_enum_result_usize__i32 main_twice__usize(__SIZE_TYPE__ x,__INT32_TYPE__ n)__asm__("_main::twice(__SIZE_TYPE__)");main_twice__usize(10,-1);}));
main_show_wide(__extension__({extern struct smith_enum_result_usize__i32 main_twice__usize(__SIZE_TYPE__ x,__INT32_TYPE__ n)__asm__("_main::twice(__SIZE_TYPE__)");main_twice__usize(10,0);}));
main_show_real(__extension__({extern struct smith_enum_result_f64__i32 main_twice__f64(double x,__INT32_TYPE__ n)__asm__("_main::twice(double)");main_twice__f64(1.5,2);}));
main_show_small(__extension__({extern struct smith_enum_option_u8 main_maybe_twice__u8(__UINT8_TYPE__ x,__INT32_TYPE__ n)__asm__("_main::maybe_twice(__UINT8_TYPE__)");main_maybe_twice__u8(3,4);}));
main_show_small(__extension__({extern struct smith_enum_option_u8 main_maybe_twice__u8(__UINT8_TYPE__ x,__INT32_TYPE__ n)__asm__("_main::maybe_twice(__UINT8_TYPE__)");main_maybe_twice__u8(3,-4);}));
main_show_float(__extension__({extern struct smith_enum_option_f64 main_maybe_twice__f64(double x,__INT32_TYPE__ n)__asm__("_main::maybe_twice(double)");main_maybe_twice__f64(1.5,4);}));
return 0;
}
extern struct smith_enum_result_usize__i32 main_twice__usize(__SIZE_TYPE__ x,__INT32_TYPE__ n)__asm__("_main::twice(__SIZE_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_result_usize__i32 main_twice__usize(__SIZE_TYPE__ x,__INT32_TYPE__ n){errdefer puts("  errdefer twice");if(n==0)return{.error=100};__INT32_TYPE__ v=try main_step(n);return{.ok=x+(__SIZE_TYPE__)v};}
extern struct smith_enum_result_f64__i32 main_twice__f64(double x,__INT32_TYPE__ n)__asm__("_main::twice(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_result_f64__i32 main_twice__f64(double x,__INT32_TYPE__ n){errdefer puts("  errdefer twice");if(n==0)return{.error=100};__INT32_TYPE__ v=try main_step(n);return{.ok=x+(double)v};}
extern struct smith_enum_option_u8 main_maybe_twice__u8(__UINT8_TYPE__ x,__INT32_TYPE__ n)__asm__("_main::maybe_twice(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_option_u8 main_maybe_twice__u8(__UINT8_TYPE__ x,__INT32_TYPE__ n){errdefer puts("  errdefer maybe_twice");__INT32_TYPE__ v=try main_pick(n);return{.some=x+(__UINT8_TYPE__)v};}
extern struct smith_enum_option_f64 main_maybe_twice__f64(double x,__INT32_TYPE__ n)__asm__("_main::maybe_twice(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_option_f64 main_maybe_twice__f64(double x,__INT32_TYPE__ n){errdefer puts("  errdefer maybe_twice");__INT32_TYPE__ v=try main_pick(n);return{.some=x+(double)v};}

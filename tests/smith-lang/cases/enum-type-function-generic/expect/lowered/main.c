#include <stdio.h>
struct smith_enum_maybe_usize {enum smith_enum_maybe_usize_tag {smith_enum_maybe_usize_tag_some, smith_enum_maybe_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_maybe_usize main_Maybe;struct smith_enum_maybe_f64 {enum smith_enum_maybe_f64_tag {smith_enum_maybe_f64_tag_some, smith_enum_maybe_f64_tag_none } tag; union { double some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_maybe_f64 main_MaybeF; [ [ clang :: unavailable ( "main::first is a generic function: call it from a .smith module" ) ] ] void main_first ( void ) ; [ [ clang :: unavailable ( "main::wrap is a generic function: call it from a .smith module" ) ] ] void main_wrap ( void ) ; [ [ clang :: unavailable ( "main::pass is a generic function: call it from a .smith module" ) ] ] void main_pass ( void ) ;
static __SIZE_TYPE__ main_or_else(main_Maybe m, __SIZE_TYPE__ otherwise) {
if (m == .some) return m.some;
return otherwise;
}
static double main_or_zero(main_MaybeF m) {
if (m == .some) return m.some;
return 0;
}
__INT32_TYPE__ main(void) {
__SIZE_TYPE__ xs[2] = {7, 8};
main_Maybe a =__extension__({extern struct smith_enum_maybe_usize main_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_main::first(__SIZE_TYPE__)");main_first__usize(xs,2);});
main_Maybe none =__extension__({extern struct smith_enum_maybe_usize main_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_main::first(__SIZE_TYPE__)");main_first__usize(xs,0);});
main_MaybeF w =smith_call_failed;
main_Maybe p =__extension__({extern struct smith_enum_maybe_usize main_pass__usize(struct smith_enum_maybe_usize o)__asm__("_main::pass(__SIZE_TYPE__)");main_pass__usize(a);});
printf("%zu %zu %g %zu\n", main_or_else(a, 0), main_or_else(none, 99), main_or_zero(w), main_or_else(p, 0));
return 0;
}
extern struct smith_enum_maybe_usize main_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_main::first(__SIZE_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_maybe_usize main_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n){if(n==0)return.none;return{.some=xs[0]};}
extern struct smith_enum_maybe_usize main_pass__usize(struct smith_enum_maybe_usize o)__asm__("_main::pass(__SIZE_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_maybe_usize main_pass__usize(struct smith_enum_maybe_usize o){struct smith_enum_maybe_usize copy=o;return copy;}

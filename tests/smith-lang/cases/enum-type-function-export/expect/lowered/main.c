#include <stdio.h>
#include "opt.h"
#include "other.h"
struct smith_enum_maybe_usize {enum smith_enum_maybe_usize_tag {smith_enum_maybe_usize_tag_some, smith_enum_maybe_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_maybe_usize main_Maybe;struct smith_enum_maybe_f64 {enum smith_enum_maybe_f64_tag {smith_enum_maybe_f64_tag_some, smith_enum_maybe_f64_tag_none } tag; union { double some; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_maybe_f64 main_MaybeF;
static __SIZE_TYPE__ main_or_else(main_Maybe m, __SIZE_TYPE__ otherwise) {
if (m == .some) return m.some;
return otherwise;
}
static double main_or_zero(main_MaybeF m) {
if (m == .some) return m.some;
return 0;
}struct smith_enum_maybe_i32 {enum smith_enum_maybe_i32_tag {smith_enum_maybe_i32_tag_some, smith_enum_maybe_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("std::enum")));
static struct smith_enum_maybe_i32 main_twice(__INT32_TYPE__ x) {
return {.some = x * 2};
}
__INT32_TYPE__ main(void) {
__SIZE_TYPE__ xs[2] = {7, 8};
main_Maybe a =__extension__({extern struct smith_enum_maybe_usize opt_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_opt::first(__SIZE_TYPE__)");opt_first__usize(xs,2);});
main_Maybe n =__extension__({extern struct smith_enum_maybe_usize opt_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_opt::first(__SIZE_TYPE__)");opt_first__usize(xs,0);});
main_Maybe f = opt_find("key=value", '=');
main_MaybeF w =__extension__({extern struct smith_enum_maybe_f64 opt_first__f64(double*xs,__SIZE_TYPE__ n)__asm__("_opt::first(double)");opt_first__f64((double[]){2.5},1);});
main_Maybe p = other_pick(5);
printf("%zu\n", main_or_else(p, 0));
printf("%zu %zu %zu %g\n", main_or_else(a, 0), main_or_else(n, 99), main_or_else(f, 99), main_or_zero(w));
return 0;
}
extern struct smith_enum_maybe_usize opt_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_opt::first(__SIZE_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_maybe_usize opt_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n){if(n==0)return.none;return{.some=xs[0]};}
extern struct smith_enum_maybe_f64 opt_first__f64(double*xs,__SIZE_TYPE__ n)__asm__("_opt::first(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_maybe_f64 opt_first__f64(double*xs,__SIZE_TYPE__ n){if(n==0)return.none;return{.some=xs[0]};}

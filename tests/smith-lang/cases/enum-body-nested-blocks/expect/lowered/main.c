#include <stdio.h>

static __SIZE_TYPE__ main_walk(__SIZE_TYPE__ n) {
__SIZE_TYPE__ total = 0;
{ struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_option_usize outer = {.some = n};
{
struct smith_enum_option_usize inner = {.some = n + 1};
if (outer == .some && inner == .some) total += outer.some + inner.some;
}
}
{ struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_option_usize later = {.some = 10};
if (later == .some) total += later.some;
}
return total;
}
__INT32_TYPE__ main(void) {
printf("%zu\n", main_walk(2));
return 0;
}

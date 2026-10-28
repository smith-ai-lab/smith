#include <stdio.h>

static __SIZE_TYPE__ main_first(Bool left) {
if (left) { struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("std::enum")));
struct smith_enum_option_usize a = {.some = 1};
if (a == .some) return a.some;
} else { struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("std::enum")));
struct smith_enum_option_usize b = {.some = 2};
if (b == .some) return b.some;
}
return 0;
}
static __SIZE_TYPE__ main_second(void) { struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("std::enum")));
struct smith_enum_option_usize c = {.some = 3};
c = .none;
if (c == .none) c = {.some = 4};
if (c == .some) return c.some;
return 0;
}
__INT32_TYPE__ main(void) {
printf("%zu %zu %zu\n", main_first(true), main_first(false), main_second());
return 0;
}

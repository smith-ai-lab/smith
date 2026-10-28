#include <stdio.h>
#include "finder.h"

__INT32_TYPE__ main(void) { struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_none, smith_enum_option_usize_tag_some } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_option_usize i = finder_find("key=value", '=');
struct smith_enum_option_usize j = .none;
j = finder_find("a,b", ',');
if (i == .some && j == .some) printf("%zu %zu\n", i.some, j.some);
return 0;
}

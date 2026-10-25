#include "opt.h"
struct smith_enum_maybe_usize {enum smith_enum_maybe_usize_tag {smith_enum_maybe_usize_tag_some, smith_enum_maybe_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_maybe_usize other_pick(__SIZE_TYPE__ v) __asm__("_other::pick");
struct smith_enum_maybe_usize other_pick(__SIZE_TYPE__ v) {
return {.some = v};
}

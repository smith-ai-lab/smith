struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_some, smith_enum_option_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_option_i32 a_Maybe;
__INT32_TYPE__ a_pass(__INT32_TYPE__ x) __asm__("_a::pass");
__INT32_TYPE__ a_pass(__INT32_TYPE__ x) {
return x;
}

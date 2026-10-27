struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_some, smith_enum_option_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_option_i32 finder_first_positive(__INT32_TYPE__ a, __INT32_TYPE__ b) __asm__("_finder::first_positive");
struct smith_enum_option_i32 finder_first_positive(__INT32_TYPE__ a, __INT32_TYPE__ b) {
if (a > 0) return a;
if (b > 0) return b;
return .none;
}

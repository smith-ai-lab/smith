struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_option_usize parser_position(const char *text, char wanted) __asm__("_parser::position");
struct smith_enum_option_usize parser_position(const char *text, char wanted) {
for (__SIZE_TYPE__ i = 0; text[i] != 0; i++)
if (text[i] == wanted) return {.some = i};
return .none;
}struct smith_enum_result_f64__i32 {enum smith_enum_result_f64__i32_tag {smith_enum_result_f64__i32_tag_ok, smith_enum_result_f64__i32_tag_error } tag; union { double ok; __INT32_TYPE__ error; }; } __attribute__((annotate("smith::enum")));
struct smith_enum_result_f64__i32 parser_divide(double a, double b) __asm__("_parser::divide");
struct smith_enum_result_f64__i32 parser_divide(double a, double b) {
if (b == 0) return {.error = 7};
return {.ok = a / b};
}

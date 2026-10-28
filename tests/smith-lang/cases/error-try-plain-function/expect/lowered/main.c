struct smith_enum_result_i32__i32 {enum smith_enum_result_i32__i32_tag {smith_enum_result_i32__i32_tag_ok, smith_enum_result_i32__i32_tag_error } tag; union { __INT32_TYPE__ ok; __INT32_TYPE__ error; }; } __attribute__((annotate("std::enum")));
static struct smith_enum_result_i32__i32 main_step(__INT32_TYPE__ n) {
return {.ok = n};
}
static __INT32_TYPE__ main_plain(void) {
__INT32_TYPE__ a = try main_step(1);
return a;
}
__INT32_TYPE__ main(void) {
return main_plain();
}

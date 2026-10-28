struct smith_enum_result_i32__i32 {enum smith_enum_result_i32__i32_tag {smith_enum_result_i32__i32_tag_error, smith_enum_result_i32__i32_tag_ok } tag; union { __INT32_TYPE__ error; __INT32_TYPE__ ok; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_result_i32__i32 main_step(__INT32_TYPE__ n) {
return {.ok = n};
}
static __INT32_TYPE__ main_twice(__INT32_TYPE__ n) {
return n * 2;
}
static struct smith_enum_result_i32__i32 main_call(void) {
__INT32_TYPE__ a = main_twice(try main_step(1));
__INT32_TYPE__ b = 1 + try main_step(2);
return {.ok = a + b};
}
__INT32_TYPE__ main(void) {
return main_call() == .ok ? 0 : 1;
}

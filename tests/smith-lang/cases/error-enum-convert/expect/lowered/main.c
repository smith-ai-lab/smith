struct smith_enum_result_i32__i32 {enum smith_enum_result_i32__i32_tag {smith_enum_result_i32__i32_tag_ok, smith_enum_result_i32__i32_tag_error } tag; union { __INT32_TYPE__ ok; __INT32_TYPE__ error; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_result_i32__i32 main_Same;struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_some, smith_enum_option_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_option_i32 main_Inner;struct smith_enum_option_main_Inner {enum smith_enum_option_main_Inner_tag {smith_enum_option_main_Inner_tag_some, smith_enum_option_main_Inner_tag_none } tag; union { main_Inner some; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_option_main_Inner main_Nested;
typedef __INT32_TYPE__ *main_Address;struct smith_enum_option_main_Address {enum smith_enum_option_main_Address_tag {smith_enum_option_main_Address_tag_some, smith_enum_option_main_Address_tag_none } tag; union { main_Address some; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_option_main_Address main_Maybe;
typedef struct smith_enum_option_i32 main_Number;
static main_Same main_same(__INT32_TYPE__ v) {
return v;
}
static main_Nested main_nested(__INT32_TYPE__ v) {
return v;
}
static main_Maybe main_maybe(void) {
return nullptr;
}
static bool main_compared(main_Number n) {
return n == 5;
}
static __INT32_TYPE__ main_unwrapped(main_Number n) {
__INT32_TYPE__ v = n;
return v;
}
__INT32_TYPE__ main(void) {
return 0;
}

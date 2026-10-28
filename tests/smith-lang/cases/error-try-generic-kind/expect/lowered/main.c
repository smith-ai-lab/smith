struct smith_enum_result_i32__i32 {enum smith_enum_result_i32__i32_tag {smith_enum_result_i32__i32_tag_ok, smith_enum_result_i32__i32_tag_error } tag; union { __INT32_TYPE__ ok; __INT32_TYPE__ error; }; } __attribute__((annotate("std::enum")));
typedef struct smith_enum_result_i32__i32 main_Out;struct smith_enum_option_i32 {enum smith_enum_option_i32_tag {smith_enum_option_i32_tag_some, smith_enum_option_i32_tag_none } tag; union { __INT32_TYPE__ some; }; } __attribute__((annotate("std::enum")));
static struct smith_enum_option_i32 main_pick(__INT32_TYPE__ n) {
if (n < 0) return .none;
return {.some = n};
} [ [ clang :: unavailable ( "main::wrong is a generic function: call it from a .smith module" ) ] ] void main_wrong ( void ) ;
__INT32_TYPE__ main(void) {
main_Out r =__extension__({extern struct smith_enum_result_i32__i32 main_wrong__I32(__INT32_TYPE__ x)__asm__("_main::wrong(__INT32_TYPE__)");main_wrong__I32(1);});
return (__INT32_TYPE__)sizeof r - (__INT32_TYPE__)sizeof r;
}
extern struct smith_enum_result_i32__i32 main_wrong__I32(__INT32_TYPE__ x)__asm__("_main::wrong(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_result_i32__i32 main_wrong__I32(__INT32_TYPE__ x){__INT32_TYPE__ v=try main_pick(1);return{.ok=x};}

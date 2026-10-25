struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));static struct smith_enum_option_usize lone_nothing(void) {
return .none;
}
typedef struct smith_enum_option_usize lone_Index;struct smith_enum_result_f64__i32 {enum smith_enum_result_f64__i32_tag {smith_enum_result_f64__i32_tag_ok, smith_enum_result_f64__i32_tag_error } tag; union { double ok; __INT32_TYPE__ error; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_result_f64__i32 lone_Quotient;
static lone_Index lone_something(__SIZE_TYPE__ at) {
return {.some = at};
}
static lone_Quotient lone_failed(void) {
return {.error = 7};
} [ [ clang :: unavailable ( "lone::first is a generic function: call it from a .smith module" ) ] ] void lone_first ( void ) ;
static __SIZE_TYPE__ lone_or_else(lone_Index at, __SIZE_TYPE__ otherwise) {
if (at == .some) return at.some;
return otherwise;
}
static __SIZE_TYPE__ lone_use_first(void) {
__SIZE_TYPE__ xs[1] = {5};
return lone_or_else(__extension__({extern struct smith_enum_option_usize lone_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_lone::first(__SIZE_TYPE__)");lone_first__usize(xs,1);}), 0);
}
extern struct smith_enum_option_usize lone_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_lone::first(__SIZE_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_option_usize lone_first__usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n){if(n==0)return.none;return{.some=xs[0]};}

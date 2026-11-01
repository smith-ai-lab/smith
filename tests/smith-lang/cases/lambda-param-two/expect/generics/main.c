__extension__({ extern __INT32_TYPE__ main_twice_call__lambda(__INT32_TYPE__ x) __asm__("_main::twice_call(lambda#36f950116bb9df34)@src/main.smith"); main_twice_call__lambda(1); })
__extension__({ extern __INT32_TYPE__ main_twice_call__lambda_2(__INT32_TYPE__ x) __asm__("_main::twice_call(lambda#41567a475d0e27cb)@src/main.smith"); main_twice_call__lambda_2(1); })
__extension__({ extern __INT32_TYPE__ main_twice_call__lambda(__INT32_TYPE__ x) __asm__("_main::twice_call(lambda#36f950116bb9df34)@src/main.smith"); main_twice_call__lambda(10); })
static I32 main_twice_call__lambda__arg0(__INT32_TYPE__ a) {
    return a + 1;
}
_Static_assert(__builtin_types_compatible_p(__typeof__(main_twice_call__lambda__arg0), __INT32_TYPE__(__INT32_TYPE__)), "smith: the argument for `f` of `twice_call` does not have the type of `Lambda(I32 (I32))`");
extern __INT32_TYPE__ main_twice_call__lambda(__INT32_TYPE__ x) __asm__("_main::twice_call(lambda#36f950116bb9df34)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ main_twice_call__lambda(__INT32_TYPE__ x) {
    return main_twice_call__lambda__arg0(main_twice_call__lambda__arg0(x));
}
static I32 main_twice_call__lambda_2__arg0(__INT32_TYPE__ a) {
    return a * 3;
}
_Static_assert(__builtin_types_compatible_p(__typeof__(main_twice_call__lambda_2__arg0), __INT32_TYPE__(__INT32_TYPE__)), "smith: the argument for `f` of `twice_call` does not have the type of `Lambda(I32 (I32))`");
extern __INT32_TYPE__ main_twice_call__lambda_2(__INT32_TYPE__ x) __asm__("_main::twice_call(lambda#41567a475d0e27cb)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ main_twice_call__lambda_2(__INT32_TYPE__ x) {
    return main_twice_call__lambda_2__arg0(main_twice_call__lambda_2__arg0(x));
}

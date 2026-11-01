__extension__({ extern main_Call main_wrap__twice(void) __asm__("_main::wrap(main_twice)@src/main.smith"); main_wrap__twice(); })
__extension__({ extern main_Call main_wrap__negate(void) __asm__("_main::wrap(main_negate)@src/main.smith"); main_wrap__negate(); })
static I32 main_wrap__twice__lambda0(__INT32_TYPE__ x) {
    return main_twice(x) + 1;
}
extern main_Call main_wrap__twice(void) __asm__("_main::wrap(main_twice)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] main_Call main_wrap__twice(void) {
    return main_wrap__twice__lambda0;
}
static I32 main_wrap__negate__lambda0(__INT32_TYPE__ x) {
    return main_negate(x) + 1;
}
extern main_Call main_wrap__negate(void) __asm__("_main::wrap(main_negate)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] main_Call main_wrap__negate(void) {
    return main_wrap__negate__lambda0;
}

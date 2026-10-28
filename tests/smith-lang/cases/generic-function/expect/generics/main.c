__extension__({ extern int apply_twice__inc_int(int x) __asm__("_apply::twice(main_inc, int)@src/main.smith"); apply_twice__inc_int(5); })
__extension__({ extern int apply_twice__applyxxtriple_int(int x) __asm__("_apply::twice(apply_triple, int)@src/main.smith"); apply_twice__applyxxtriple_int(2); })
__extension__({ extern double apply_twice__half_F64(double x) __asm__("_apply::twice(main_half, double)@src/main.smith"); apply_twice__half_F64(10.0); })
__extension__({ extern int apply_twice__abs_int(int x) __asm__("_apply::twice(abs, int)@src/main.smith"); apply_twice__abs_int(- 3); })
__extension__({ extern int apply_again__inc_int(int x) __asm__("_apply::again(main_inc, int)@src/main.smith"); apply_again__inc_int(0); })
__extension__({ extern __SIZE_TYPE__ apply_address_matches__inc_int(int x) __asm__("_apply::address_matches(main_inc, int)@src/main.smith"); apply_address_matches__inc_int(1); })
__extension__({ extern double apply_call_with_pair__plus_int(int x, int y) __asm__("_apply::call_with_pair(main_plus, int)@src/main.smith"); apply_call_with_pair__plus_int(3, 4); })
extern int apply_twice__inc_int(int x) __asm__("_apply::twice(main_inc, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] int apply_twice__inc_int(int x) {
    return main_inc(main_inc(x));
}
extern int apply_twice__applyxxtriple_int(int x) __asm__("_apply::twice(apply_triple, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] int apply_twice__applyxxtriple_int(int x) {
    return apply_triple(apply_triple(x));
}
extern double apply_twice__half_F64(double x) __asm__("_apply::twice(main_half, double)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] double apply_twice__half_F64(double x) {
    return main_half(main_half(x));
}
extern int apply_twice__abs_int(int x) __asm__("_apply::twice(abs, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] int apply_twice__abs_int(int x) {
    return abs(abs(x));
}
extern int apply_again__inc_int(int x) __asm__("_apply::again(main_inc, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] int apply_again__inc_int(int x) {
    return
    __extension__({ extern int apply_twice__inc_int(int x) __asm__("_apply::twice(main_inc, int)@src/main.smith"); apply_twice__inc_int(x); });
}
extern __SIZE_TYPE__ apply_address_matches__inc_int(int x) __asm__("_apply::address_matches(main_inc, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ apply_address_matches__inc_int(int x) {
    (void) x;
    return(void *) main_inc == (void *) & main_inc ? 1 :0;
}
extern double apply_call_with_pair__plus_int(int x, int y) __asm__("_apply::call_with_pair(main_plus, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] double apply_call_with_pair__plus_int(int x, int y) {
    return(double) main_plus(x, y);
}

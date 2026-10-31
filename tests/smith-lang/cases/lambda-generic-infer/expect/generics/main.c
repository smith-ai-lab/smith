__extension__({ extern __INT32_TYPE__ main_sum__i32(__INT32_TYPE__ a, __INT32_TYPE__ b) __asm__("_main::sum(int)"); main_sum__i32(2, 3); })
__extension__({ extern double main_sum__f64(double a, double b) __asm__("_main::sum(double)"); main_sum__f64(1.5, 2.0); })
typedef struct {
    __INT32_TYPE__ x;
    __INT32_TYPE__ y;
}
main_sum__i32__lambda0_args;
static __typeof_unqual__(((main_sum__i32__lambda0_args) { }.x) + ((main_sum__i32__lambda0_args) { }.y)) main_sum__i32__lambda0(__INT32_TYPE__ x, __INT32_TYPE__ y) {
    return x + y;
}
static void main_sum__i32__lambda1(void) {
}
extern __INT32_TYPE__ main_sum__i32(__INT32_TYPE__ a, __INT32_TYPE__ b) __asm__("_main::sum(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ main_sum__i32(__INT32_TYPE__ a, __INT32_TYPE__ b) {
    __INT32_TYPE__(* add)(__INT32_TYPE__, __INT32_TYPE__) = main_sum__i32__lambda0;
    void(* nothing)(void) = main_sum__i32__lambda1;
    nothing();
    return add(a, b);
}
typedef struct {
    double x;
    double y;
}
main_sum__f64__lambda0_args;
static __typeof_unqual__(((main_sum__f64__lambda0_args) { }.x) + ((main_sum__f64__lambda0_args) { }.y)) main_sum__f64__lambda0(double x, double y) {
    return x + y;
}
static void main_sum__f64__lambda1(void) {
}
extern double main_sum__f64(double a, double b) __asm__("_main::sum(double)");
[[gnu::weak]][[gnu::visibility("hidden")]] double main_sum__f64(double a, double b) {
    double(* add)(double, double) = main_sum__f64__lambda0;
    void(* nothing)(void) = main_sum__f64__lambda1;
    nothing();
    return add(a, b);
}

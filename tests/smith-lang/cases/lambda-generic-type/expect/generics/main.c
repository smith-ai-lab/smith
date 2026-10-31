__extension__({ extern main_Size main_size_of_type__i16(void) __asm__("_main::size_of_type(short)"); main_size_of_type__i16(); })
__extension__({ extern main_Size main_size_of_type__f64(void) __asm__("_main::size_of_type(double)"); main_size_of_type__f64(); })
static i32 main_size_of_type__i16__lambda0(void) {
    return(__INT32_TYPE__) sizeof(__INT16_TYPE__);
}
extern main_Size main_size_of_type__i16(void) __asm__("_main::size_of_type(short)");
[[gnu::weak]][[gnu::visibility("hidden")]] main_Size main_size_of_type__i16(void) {
    return main_size_of_type__i16__lambda0;
}
static i32 main_size_of_type__f64__lambda0(void) {
    return(__INT32_TYPE__) sizeof(double);
}
extern main_Size main_size_of_type__f64(void) __asm__("_main::size_of_type(double)");
[[gnu::weak]][[gnu::visibility("hidden")]] main_Size main_size_of_type__f64(void) {
    return main_size_of_type__f64__lambda0;
}

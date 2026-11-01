__extension__({ extern __SIZE_TYPE__ args_count__int(int x) __asm__("_args::count(int)"); args_count__int(1); })
__extension__({ extern __SIZE_TYPE__ args_count__int_f64(int x, [[maybe_unused]] double rest_0) __asm__("_args::count(int, double)"); args_count__int_f64(1, 2.5); })
__extension__({ extern __SIZE_TYPE__ args_count__int_f64_u8_char_P(int x, [[maybe_unused]] double rest_0, [[maybe_unused]] U8 rest_1, [[maybe_unused]] char * rest_2) __asm__("_args::count(int, double, unsigned char, char *)"); args_count__int_f64_u8_char_P(1, 2.5, (__UINT8_TYPE__) 3, "four"); })
__extension__({ extern int args_call_all__add1_int(int x) __asm__("_args::call_all(main_add1, int)@src/main.smith"); args_call_all__add1_int(5); })
__extension__({ extern int args_call_all__plus_int_int(int x, [[maybe_unused]] int rest_0) __asm__("_args::call_all(main_plus, int, int)@src/main.smith"); args_call_all__plus_int_int(5, 6); })
__extension__({ extern int args_call_all__add3_int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_args::call_all(main_add3, int, int, int)@src/main.smith"); args_call_all__add3_int_int_int(5, 6, 7); })
__extension__({ extern double args_second_or__int(int x) __asm__("_args::second_or(int)"); args_second_or__int(1); })
__extension__({ extern double args_second_or__int_int(int x, [[maybe_unused]] int rest_0) __asm__("_args::second_or(int, int)"); args_second_or__int_int(1, 2); })
__extension__({ extern double args_second_or__int_int_f32(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] float rest_1) __asm__("_args::second_or(int, int, float)"); args_second_or__int_int_f32(1, 2, 3.5f); })
__extension__({ extern __SIZE_TYPE__ args_size_of_first__(void) __asm__("_args::size_of_first()"); args_size_of_first__(); })
__extension__({ extern __SIZE_TYPE__ args_size_of_first__u16_f64([[maybe_unused]] U16 rest_0, [[maybe_unused]] double rest_1) __asm__("_args::size_of_first(unsigned short, double)"); args_size_of_first__u16_f64((__UINT16_TYPE__) 1, 2.0); })
__extension__({ extern __SIZE_TYPE__ args_size_of_first__f64_u16([[maybe_unused]] double rest_0, [[maybe_unused]] U16 rest_1) __asm__("_args::size_of_first(double, unsigned short)"); args_size_of_first__f64_u16(2.0, (__UINT16_TYPE__) 1); })
__extension__({ extern int args_forward__add3_int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_args::forward(main_add3, int, int, int)@src/main.smith"); args_forward__add3_int_int_int(1, 2, 3); })
__extension__({ extern int args_forward__add1_int(int x) __asm__("_args::forward(main_add1, int)@src/main.smith"); args_forward__add1_int(1); })
__extension__({ extern __SIZE_TYPE__ args_forward_count__int(int x) __asm__("_args::forward_count(int)"); args_forward_count__int(1); })
__extension__({ extern __SIZE_TYPE__ args_forward_count__int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_args::forward_count(int, int, int)"); args_forward_count__int_int_int(1, 2, 3); })
__extension__({ extern __SIZE_TYPE__ args_at_least_two__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) __asm__("_args::at_least_two(int, int, int)"); args_at_least_two__int_int_int(1, 2, 3); })
extern __SIZE_TYPE__ args_count__int(int x) __asm__("_args::count(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_count__int(int x) {
    (void) x;
    return 0;
}
extern __SIZE_TYPE__ args_count__int_f64(int x, [[maybe_unused]] double rest_0) __asm__("_args::count(int, double)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_count__int_f64(int x, [[maybe_unused]] double rest_0) {
    (void) x;
    return 1;
}
extern __SIZE_TYPE__ args_count__int_f64_u8_char_P(int x, [[maybe_unused]] double rest_0, [[maybe_unused]] U8 rest_1, [[maybe_unused]] char * rest_2) __asm__("_args::count(int, double, unsigned char, char *)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_count__int_f64_u8_char_P(int x, [[maybe_unused]] double rest_0, [[maybe_unused]] U8 rest_1, [[maybe_unused]] char * rest_2) {
    (void) x;
    return 3;
}
extern int args_call_all__add1_int(int x) __asm__("_args::call_all(main_add1, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] int args_call_all__add1_int(int x) {
    (void) sizeof(main_add1(x));
    return main_add1(x);
}
extern int args_call_all__plus_int_int(int x, [[maybe_unused]] int rest_0) __asm__("_args::call_all(main_plus, int, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] int args_call_all__plus_int_int(int x, [[maybe_unused]] int rest_0) {
    (void) sizeof(main_plus(x, rest_0));
    return main_plus(x, rest_0);
}
extern int args_call_all__add3_int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_args::call_all(main_add3, int, int, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] int args_call_all__add3_int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    (void) sizeof(main_add3(x, rest_0, rest_1));
    return main_add3(x, rest_0, rest_1);
}
extern double args_second_or__int(int x) __asm__("_args::second_or(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] double args_second_or__int(int x) {
    (void) x;
    {
        return - 1.0;
    }
}
extern double args_second_or__int_int(int x, [[maybe_unused]] int rest_0) __asm__("_args::second_or(int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] double args_second_or__int_int(int x, [[maybe_unused]] int rest_0) {
    (void) x;
    {
        return - 1.0;
    }
}
extern double args_second_or__int_int_f32(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] float rest_1) __asm__("_args::second_or(int, int, float)");
[[gnu::weak]][[gnu::visibility("hidden")]] double args_second_or__int_int_f32(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] float rest_1) {
    (void) x;
    {
        return(double) rest_1;
    }
}
extern __SIZE_TYPE__ args_size_of_first__(void) __asm__("_args::size_of_first()");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_size_of_first__(void) {
    {
        return 0;
    }
}
extern __SIZE_TYPE__ args_size_of_first__u16_f64([[maybe_unused]] U16 rest_0, [[maybe_unused]] double rest_1) __asm__("_args::size_of_first(unsigned short, double)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_size_of_first__u16_f64([[maybe_unused]] U16 rest_0, [[maybe_unused]] double rest_1) {
    {
        return sizeof(rest_0);
    }
}
extern __SIZE_TYPE__ args_size_of_first__f64_u16([[maybe_unused]] double rest_0, [[maybe_unused]] U16 rest_1) __asm__("_args::size_of_first(double, unsigned short)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_size_of_first__f64_u16([[maybe_unused]] double rest_0, [[maybe_unused]] U16 rest_1) {
    {
        return sizeof(rest_0);
    }
}
extern int args_forward__add3_int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_args::forward(main_add3, int, int, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] int args_forward__add3_int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    return
    __extension__({ extern int args_call_all__add3_int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_args::call_all(main_add3, int, int, int)@src/main.smith"); args_call_all__add3_int_int_int(x, rest_0, rest_1); });
}
extern int args_forward__add1_int(int x) __asm__("_args::forward(main_add1, int)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] int args_forward__add1_int(int x) {
    return
    __extension__({ extern int args_call_all__add1_int(int x) __asm__("_args::call_all(main_add1, int)@src/main.smith"); args_call_all__add1_int(x); });
}
extern __SIZE_TYPE__ args_forward_count__int(int x) __asm__("_args::forward_count(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_forward_count__int(int x) {
    return
    __extension__({ extern __SIZE_TYPE__ args_count__int(int x) __asm__("_args::count(int)"); args_count__int(x); }) + 100;
}
extern __SIZE_TYPE__ args_forward_count__int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_args::forward_count(int, int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_forward_count__int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    return
    __extension__({ extern __SIZE_TYPE__ args_count__int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_args::count(int, int, int)"); args_count__int_int_int(x, rest_0, rest_1); }) + 100;
}
extern __SIZE_TYPE__ args_at_least_two__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) __asm__("_args::at_least_two(int, int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_at_least_two__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) {
    static_assert(3 >= 2, "at_least_two needs two arguments");
    __UINT8_TYPE__ bytes[3];
    return sizeof bytes;
}
extern __SIZE_TYPE__ args_count__int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_args::count(int, int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ args_count__int_int_int(int x, [[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    (void) x;
    return 2;
}

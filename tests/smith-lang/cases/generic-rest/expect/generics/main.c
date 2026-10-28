__extension__({ extern __INT32_TYPE__ arguments_total__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) __asm__("_arguments::total(int, int, int)"); arguments_total__int_int_int(1, 2, 3); })
__extension__({ extern __INT32_TYPE__ arguments_total__(void) __asm__("_arguments::total()"); arguments_total__(); })
__extension__({ extern __INT32_TYPE__ arguments_total__U8_I64_F64([[maybe_unused]] U8 rest_0, [[maybe_unused]] I64 rest_1, [[maybe_unused]] double rest_2) __asm__("_arguments::total(unsigned char, long long, double)"); arguments_total__U8_I64_F64((__UINT8_TYPE__) 200, (__INT64_TYPE__) 5, 2.5); })
__extension__({ extern __INT32_TYPE__ arguments_weighted__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_arguments::weighted(int, int)"); arguments_weighted__int_int(10, 20); })
__extension__({ extern __INT32_TYPE__ arguments_spread__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_arguments::spread(int, int)"); arguments_spread__int_int(5, 6); })
__extension__({ extern __INT32_TYPE__ arguments_spread__int([[maybe_unused]] int rest_0) __asm__("_arguments::spread(int)"); arguments_spread__int(5); })
__extension__({ extern __INT32_TYPE__ arguments_shapes__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_arguments::shapes(int, int)"); arguments_shapes__int_int(1, 2); })
__extension__({ extern __INT32_TYPE__ arguments_shapes__(void) __asm__("_arguments::shapes()"); arguments_shapes__(); })
__extension__({ extern __INT32_TYPE__ arguments_mixed__int_U16_F64([[maybe_unused]] int rest_0, [[maybe_unused]] U16 rest_1, [[maybe_unused]] double rest_2) __asm__("_arguments::mixed(int, unsigned short, double)"); arguments_mixed__int_U16_F64(1, (__UINT16_TYPE__) 2, 2.5); })
extern __INT32_TYPE__ arguments_total__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) __asm__("_arguments::total(int, int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ arguments_total__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) {
    __INT32_TYPE__ sum = 0;
    {
        sum += (__INT32_TYPE__) rest_0;
    }
    {
        sum += (__INT32_TYPE__) rest_1;
    }
    {
        sum += (__INT32_TYPE__) rest_2;
    }
    return sum;
}
extern __INT32_TYPE__ arguments_total__(void) __asm__("_arguments::total()");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ arguments_total__(void) {
    __INT32_TYPE__ sum = 0;
    return sum;
}
extern __INT32_TYPE__ arguments_total__U8_I64_F64([[maybe_unused]] U8 rest_0, [[maybe_unused]] I64 rest_1, [[maybe_unused]] double rest_2) __asm__("_arguments::total(unsigned char, long long, double)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ arguments_total__U8_I64_F64([[maybe_unused]] U8 rest_0, [[maybe_unused]] I64 rest_1, [[maybe_unused]] double rest_2) {
    __INT32_TYPE__ sum = 0;
    {
        sum += (__INT32_TYPE__) rest_0;
    }
    {
        sum += (__INT32_TYPE__) rest_1;
    }
    {
        sum += (__INT32_TYPE__) rest_2;
    }
    return sum;
}
extern __INT32_TYPE__ arguments_weighted__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_arguments::weighted(int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ arguments_weighted__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    __INT32_TYPE__ weights[] = {
        1, 2, 3, 4 }
    ;
    static_assert(sizeof weights / sizeof weights[0] == 4);
    __INT32_TYPE__ sum = 0;
    {
        sum += (__INT32_TYPE__) rest_0 * weights[0];
    }
    {
        sum += (__INT32_TYPE__) rest_1 * weights[1];
    }
    return sum;
}
extern __INT32_TYPE__ arguments_spread__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_arguments::spread(int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ arguments_spread__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    static_assert(2 <= 4);
    __INT32_TYPE__ table[] = {
        100, rest_0, rest_1, 7 }
    ;
    {
        char text[64];
        snprintf(text, sizeof text, "%d %d", rest_0, rest_1);
        printf("[%s]\n", text);
    }
    return table[2 + 1] + (__INT32_TYPE__)(sizeof table / sizeof table[0]);
}
extern __INT32_TYPE__ arguments_spread__int([[maybe_unused]] int rest_0) __asm__("_arguments::spread(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ arguments_spread__int([[maybe_unused]] int rest_0) {
    static_assert(1 <= 4);
    __INT32_TYPE__ table[] = {
        100, rest_0, 7 }
    ;
    return table[1 + 1] + (__INT32_TYPE__)(sizeof table / sizeof table[0]);
}
extern __INT32_TYPE__ arguments_shapes__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_arguments::shapes(int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ arguments_shapes__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    __INT32_TYPE__ sum = 0;
    {
        sum += (__INT32_TYPE__) rest_1 * (__INT32_TYPE__) 2;
    }
    {
        sum += (__INT32_TYPE__) rest_0 * (__INT32_TYPE__) 1;
    }
    {
        sum += 1000;
    }
    return sum;
}
extern __INT32_TYPE__ arguments_shapes__(void) __asm__("_arguments::shapes()");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ arguments_shapes__(void) {
    __INT32_TYPE__ sum = 0;
    return sum;
}
extern __INT32_TYPE__ arguments_mixed__int_U16_F64([[maybe_unused]] int rest_0, [[maybe_unused]] U16 rest_1, [[maybe_unused]] double rest_2) __asm__("_arguments::mixed(int, unsigned short, double)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ arguments_mixed__int_U16_F64([[maybe_unused]] int rest_0, [[maybe_unused]] U16 rest_1, [[maybe_unused]] double rest_2) {
    __INT32_TYPE__ sum = 0;
    {
        {
            sum += (__INT32_TYPE__) rest_0;
        }
        {
            {
                sum += 100;
            }
            sum += (__INT32_TYPE__) rest_0;
        }
    }
    {
        {
            sum += (__INT32_TYPE__) rest_1;
        }
        {
            sum += (__INT32_TYPE__) rest_1;
        }
    }
    {
        {
            sum += (__INT32_TYPE__) rest_2;
        }
        {
            sum += (__INT32_TYPE__) rest_2;
        }
    }
    return sum;
}

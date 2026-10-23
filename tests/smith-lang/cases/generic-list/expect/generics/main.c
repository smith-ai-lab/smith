__extension__({ extern __INT32_TYPE__ lists_total__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) __asm__("_lists::total(int, int, int)"); lists_total__int_int_int(1, 2, 3); })
__extension__({ extern __INT32_TYPE__ lists_total__(void) __asm__("_lists::total()"); lists_total__(); })
__extension__({ extern __INT32_TYPE__ lists_total__u8_i64_f64([[maybe_unused]] u8 rest_0, [[maybe_unused]] i64 rest_1, [[maybe_unused]] double rest_2) __asm__("_lists::total(unsigned char, long long, double)"); lists_total__u8_i64_f64((__UINT8_TYPE__) 200, (__INT64_TYPE__) 5, 2.5); })
__extension__({ extern __INT32_TYPE__ lists_weighted__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_lists::weighted(int, int)"); lists_weighted__int_int(10, 20); })
__extension__({ extern __INT32_TYPE__ lists_interleaved__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_lists::interleaved(int, int)"); lists_interleaved__int_int(5, 6); })
__extension__({ extern __INT32_TYPE__ lists_interleaved__int([[maybe_unused]] int rest_0) __asm__("_lists::interleaved(int)"); lists_interleaved__int(5); })
__extension__({ extern __INT32_TYPE__ lists_shapes__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_lists::shapes(int, int)"); lists_shapes__int_int(1, 2); })
__extension__({ extern __INT32_TYPE__ lists_shapes__(void) __asm__("_lists::shapes()"); lists_shapes__(); })
__extension__({ extern __INT32_TYPE__ lists_mixed__int_u16([[maybe_unused]] int rest_0, [[maybe_unused]] u16 rest_1) __asm__("_lists::mixed(int, unsigned short)"); lists_mixed__int_u16(1, (__UINT16_TYPE__) 2); })
extern __INT32_TYPE__ lists_total__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) __asm__("_lists::total(int, int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ lists_total__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) {
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
extern __INT32_TYPE__ lists_total__(void) __asm__("_lists::total()");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ lists_total__(void) {
    __INT32_TYPE__ sum = 0;
    return sum;
}
extern __INT32_TYPE__ lists_total__u8_i64_f64([[maybe_unused]] u8 rest_0, [[maybe_unused]] i64 rest_1, [[maybe_unused]] double rest_2) __asm__("_lists::total(unsigned char, long long, double)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ lists_total__u8_i64_f64([[maybe_unused]] u8 rest_0, [[maybe_unused]] i64 rest_1, [[maybe_unused]] double rest_2) {
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
extern __INT32_TYPE__ lists_weighted__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_lists::weighted(int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ lists_weighted__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    [[maybe_unused]] auto weights_0_0 = 1;
    [[maybe_unused]] auto weights_0_1 = 2;
    [[maybe_unused]] auto weights_0_2 = 3;
    [[maybe_unused]] auto weights_0_3 = 4;
    static_assert(4 == 4);
    __INT32_TYPE__ sum = 0;
    {
        sum += (__INT32_TYPE__) rest_0 * weights_0_0;
    }
    {
        sum += (__INT32_TYPE__) rest_1 * weights_0_1;
    }
    return sum;
}
extern __INT32_TYPE__ lists_interleaved__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_lists::interleaved(int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ lists_interleaved__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    [[maybe_unused]] auto args_0_0 = 100;
    [[maybe_unused]] auto args_1_0 = args_0_0;
    [[maybe_unused]] auto args_1_1 = (__INT32_TYPE__) 0;
    [[maybe_unused]] auto args_1_2 = rest_0;
    [[maybe_unused]] auto args_2_0 = args_1_0;
    [[maybe_unused]] auto args_2_1 = args_1_1;
    [[maybe_unused]] auto args_2_2 = args_1_2;
    [[maybe_unused]] auto args_2_3 = (__INT32_TYPE__) 1;
    [[maybe_unused]] auto args_2_4 = rest_1;
    static_assert(5 == 1 + 2 * 2);
    {
        char text[64];
        snprintf(text, sizeof text, "%d %d %d %d %d", args_2_0, args_2_1, args_2_2, args_2_3, args_2_4);
        printf("[%s]\n", text);
    }
    return args_2_4;
}
extern __INT32_TYPE__ lists_interleaved__int([[maybe_unused]] int rest_0) __asm__("_lists::interleaved(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ lists_interleaved__int([[maybe_unused]] int rest_0) {
    [[maybe_unused]] auto args_0_0 = 100;
    [[maybe_unused]] auto args_1_0 = args_0_0;
    [[maybe_unused]] auto args_1_1 = (__INT32_TYPE__) 0;
    [[maybe_unused]] auto args_1_2 = rest_0;
    static_assert(3 == 1 + 2 * 1);
    return args_1_2;
}
extern __INT32_TYPE__ lists_shapes__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_lists::shapes(int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ lists_shapes__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    [[maybe_unused]] auto both_0_0 = rest_0;
    [[maybe_unused]] auto both_0_1 = rest_1;
    [[maybe_unused]] auto both_0_2 = 7;
    __INT32_TYPE__ sum = 0;
    {
        sum += (__INT32_TYPE__) both_0_2 * (__INT32_TYPE__) 3;
    }
    {
        sum += (__INT32_TYPE__) both_0_1 * (__INT32_TYPE__) 2;
    }
    {
        sum += (__INT32_TYPE__) both_0_0 * (__INT32_TYPE__) 1;
    }
    {
        sum += 1000;
    }
    {
        sum += 1000;
    }
    return sum + (__INT32_TYPE__) 0;
}
extern __INT32_TYPE__ lists_shapes__(void) __asm__("_lists::shapes()");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ lists_shapes__(void) {
    [[maybe_unused]] auto both_0_0 = 7;
    __INT32_TYPE__ sum = 0;
    {
        sum += (__INT32_TYPE__) both_0_0 * (__INT32_TYPE__) 1;
    }
    {
        sum += 1000;
    }
    return sum + (__INT32_TYPE__) 0;
}
extern __INT32_TYPE__ lists_mixed__int_u16([[maybe_unused]] int rest_0, [[maybe_unused]] u16 rest_1) __asm__("_lists::mixed(int, unsigned short)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ lists_mixed__int_u16([[maybe_unused]] int rest_0, [[maybe_unused]] u16 rest_1) {
    [[maybe_unused]] auto things_0_0 = rest_0;
    [[maybe_unused]] auto things_0_1 = rest_1;
    [[maybe_unused]] auto things_0_2 = 2.5;
    [[maybe_unused]] auto things_0_3 = 'x';
    __INT32_TYPE__ sum = 0;
    {
        {
            sum += (__INT32_TYPE__) things_0_0;
        }
        {
            {
                sum += 100;
            }
            sum += (__INT32_TYPE__) things_0_0;
        }
    }
    {
        {
            sum += (__INT32_TYPE__) things_0_1;
        }
        {
            sum += (__INT32_TYPE__) things_0_1;
        }
    }
    {
        {
            sum += (__INT32_TYPE__) things_0_2;
        }
        {
            sum += (__INT32_TYPE__) things_0_2;
        }
    }
    {
        {
            sum += (__INT32_TYPE__) things_0_3;
        }
        {
            sum += (__INT32_TYPE__) things_0_3;
        }
    }
    return sum;
}

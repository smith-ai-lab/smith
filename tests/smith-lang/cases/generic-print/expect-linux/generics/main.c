__extension__({ extern void fmt_print__(void) __asm__("_fmt::print()"); fmt_print__(); })
__extension__({ extern void fmt_print__char_P([[maybe_unused]] char * args_0) __asm__("_fmt::print(char *)"); fmt_print__char_P("hello"); })
__extension__({ extern void fmt_print__char_P_int_char_P_f64_char_P_bool([[maybe_unused]] char * args_0, [[maybe_unused]] int args_1, [[maybe_unused]] char * args_2, [[maybe_unused]] double args_3, [[maybe_unused]] char * args_4, [[maybe_unused]] bool args_5) __asm__("_fmt::print(char *, int, char *, double, char *, bool)"); fmt_print__char_P_int_char_P_f64_char_P_bool("x =", 42, "y =", 2.5, "ok:", true); })
__extension__({ extern void fmt_print__u8_i64_f32_u64([[maybe_unused]] U8 args_0, [[maybe_unused]] I64 args_1, [[maybe_unused]] float args_2, [[maybe_unused]] U64 args_3) __asm__("_fmt::print(unsigned char, long, float, unsigned long)"); fmt_print__u8_i64_f32_u64(small, negative, 1.5f, (__UINT64_TYPE__) 18446744073709551615ULL); })
extern void fmt_print__(void) __asm__("_fmt::print()");
[[gnu::weak]][[gnu::visibility("hidden")]] void fmt_print__(void) {
    putchar('\n');
}
extern void fmt_print__char_P([[maybe_unused]] char * args_0) __asm__("_fmt::print(char *)");
[[gnu::weak]][[gnu::visibility("hidden")]] void fmt_print__char_P([[maybe_unused]] char * args_0) {
    {
        {
            fputs(args_0, stdout);
        }
    }
    putchar('\n');
}
extern void fmt_print__char_P_int_char_P_f64_char_P_bool([[maybe_unused]] char * args_0, [[maybe_unused]] int args_1, [[maybe_unused]] char * args_2, [[maybe_unused]] double args_3, [[maybe_unused]] char * args_4, [[maybe_unused]] bool args_5) __asm__("_fmt::print(char *, int, char *, double, char *, bool)");
[[gnu::weak]][[gnu::visibility("hidden")]] void fmt_print__char_P_int_char_P_f64_char_P_bool([[maybe_unused]] char * args_0, [[maybe_unused]] int args_1, [[maybe_unused]] char * args_2, [[maybe_unused]] double args_3, [[maybe_unused]] char * args_4, [[maybe_unused]] bool args_5) {
    {
        {
            fputs(args_0, stdout);
        }
    }
    {
        {
            putchar(' ');
        }
        {
            printf("%lld", (long long) args_1);
        }
    }
    {
        {
            putchar(' ');
        }
        {
            fputs(args_2, stdout);
        }
    }
    {
        {
            putchar(' ');
        }
        {
            printf("%g", (double) args_3);
        }
    }
    {
        {
            putchar(' ');
        }
        {
            fputs(args_4, stdout);
        }
    }
    {
        {
            putchar(' ');
        }
        {
            fputs(args_5 ? "true" :"false", stdout);
        }
    }
    putchar('\n');
}
extern void fmt_print__u8_i64_f32_u64([[maybe_unused]] U8 args_0, [[maybe_unused]] I64 args_1, [[maybe_unused]] float args_2, [[maybe_unused]] U64 args_3) __asm__("_fmt::print(unsigned char, long, float, unsigned long)");
[[gnu::weak]][[gnu::visibility("hidden")]] void fmt_print__u8_i64_f32_u64([[maybe_unused]] U8 args_0, [[maybe_unused]] I64 args_1, [[maybe_unused]] float args_2, [[maybe_unused]] U64 args_3) {
    {
        {
            printf("%llu", (unsigned long long) args_0);
        }
    }
    {
        {
            putchar(' ');
        }
        {
            printf("%lld", (long long) args_1);
        }
    }
    {
        {
            putchar(' ');
        }
        {
            printf("%g", (double) args_2);
        }
    }
    {
        {
            putchar(' ');
        }
        {
            printf("%llu", (unsigned long long) args_3);
        }
    }
    putchar('\n');
}

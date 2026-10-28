__extension__({ extern typeof_unqual(* (* (const I32 * *) 0)) smith_tools_shifted__const_i32_P(const I32 * x) __asm__("_smith::tools::shifted(const int *)"); smith_tools_shifted__const_i32_P(& value); })
__extension__({ extern typeof_unqual(* (* (const I32 * *) 0)) smith_tools_shifted__const_i32_P_int_f64(const I32 * x, [[maybe_unused]] int rest_0, [[maybe_unused]] double rest_1) __asm__("_smith::tools::shifted(const int *, int, double)"); smith_tools_shifted__const_i32_P_int_f64(& value, 1, 2.5); })
__extension__({ extern __INT32_TYPE__ smith_tools_table_sum__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) __asm__("_smith::tools::table_sum(int, int, int)"); smith_tools_table_sum__int_int_int(1, 2, 3); })
__extension__({ extern __INT32_TYPE__ smith_tools_table_sum__(void) __asm__("_smith::tools::table_sum()"); smith_tools_table_sum__(); })
__extension__({ extern __INT32_TYPE__ smith_tools_nested__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_smith::tools::nested(int, int)"); smith_tools_nested__int_int(7, 8); })
extern typeof_unqual(* (* (const I32 * *) 0)) smith_tools_shifted__const_i32_P(const I32 * x) __asm__("_smith::tools::shifted(const int *)");
[[gnu::weak]][[gnu::visibility("hidden")]] typeof_unqual(* (* (const I32 * *) 0)) smith_tools_shifted__const_i32_P(const I32 * x) {
    typeof_unqual(* x) result = * x + smith_tools_offset + smith_tools_pick();
    return result + (__INT32_TYPE__) 0;
}
extern typeof_unqual(* (* (const I32 * *) 0)) smith_tools_shifted__const_i32_P_int_f64(const I32 * x, [[maybe_unused]] int rest_0, [[maybe_unused]] double rest_1) __asm__("_smith::tools::shifted(const int *, int, double)");
[[gnu::weak]][[gnu::visibility("hidden")]] typeof_unqual(* (* (const I32 * *) 0)) smith_tools_shifted__const_i32_P_int_f64(const I32 * x, [[maybe_unused]] int rest_0, [[maybe_unused]] double rest_1) {
    typeof_unqual(* x) result = * x + smith_tools_offset + smith_tools_pick();
    return result + (__INT32_TYPE__) 2;
}
extern __INT32_TYPE__ smith_tools_table_sum__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) __asm__("_smith::tools::table_sum(int, int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ smith_tools_table_sum__int_int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1, [[maybe_unused]] int rest_2) {
    __INT32_TYPE__ table[] = {
        rest_0, rest_1, rest_2, 100 }
    ;
    __INT32_TYPE__ sum = 0;
    for(__SIZE_TYPE__ i = 0; i < sizeof table / sizeof table[0]; i ++) sum += table[i];
    return sum;
}
extern __INT32_TYPE__ smith_tools_table_sum__(void) __asm__("_smith::tools::table_sum()");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ smith_tools_table_sum__(void) {
    __INT32_TYPE__ table[] = {
        100 }
    ;
    __INT32_TYPE__ sum = 0;
    for(__SIZE_TYPE__ i = 0; i < sizeof table / sizeof table[0]; i ++) sum += table[i];
    return sum;
}
extern __INT32_TYPE__ smith_tools_nested__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) __asm__("_smith::tools::nested(int, int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ smith_tools_nested__int_int([[maybe_unused]] int rest_0, [[maybe_unused]] int rest_1) {
    __INT32_TYPE__ table[] = {
        rest_0, rest_1, 5, 1 }
    ;
    return table[0] + table[sizeof table / sizeof table[0] - 1] + (__INT32_TYPE__) 2 + 1;
}

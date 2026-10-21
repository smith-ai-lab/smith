__extension__({ extern i32 pick_larger__i32(i32 a, i32 b) __asm__("_pick::larger(int)"); pick_larger__i32(a, b); })
__extension__({ extern f64 pick_larger__f64(f64 a, f64 b) __asm__("_pick::larger(double)"); pick_larger__f64(d, 1.5); })
__extension__({ extern i32 pick_larger__i32(i32 a, i32 b) __asm__("_pick::larger(int)"); pick_larger__i32(p.x, pp->y); })
__extension__({ extern i32 pick_larger__i32(i32 a, i32 b) __asm__("_pick::larger(int)"); pick_larger__i32(p.x, 3); })
__extension__({ extern i32 pick_larger__i32(i32 a, i32 b) __asm__("_pick::larger(int)"); pick_larger__i32(numbers[1], fixed); })
__extension__({ extern i32 pick_larger__i32(i32 a, i32 b) __asm__("_pick::larger(int)"); pick_larger__i32(a, 100); })
__extension__({ extern f64 pick_larger__f64(f64 a, f64 b) __asm__("_pick::larger(double)"); pick_larger__f64(half(9), d); })
__extension__({ extern __SIZE_TYPE__ pick_size_of_value__u8(u8 value) __asm__("_pick::size_of_value(unsigned char)"); pick_size_of_value__u8(byte); })
__extension__({ extern i32 pick_first_of__i32(const i32 * values, __SIZE_TYPE__ count) __asm__("_pick::first_of(int)"); pick_first_of__i32(numbers, 3); })
__extension__({ extern __SIZE_TYPE__ pick_size_of_value__unsigned_short(unsigned short value) __asm__("_pick::size_of_value(unsigned short)"); pick_size_of_value__unsigned_short((__UINT16_TYPE__) 3); })
__extension__({ extern void pick_set__i32(i32 * out, i32 value) __asm__("_pick::set(int)"); pick_set__i32(& slot, 42); })
__extension__({ extern __INT32_TYPE__ pick_twice__i32(__INT32_TYPE__ value) __asm__("_pick::twice(int)"); pick_twice__i32(slot); })
extern i32 pick_larger__i32(i32 a, i32 b) __asm__("_pick::larger(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] i32 pick_larger__i32(i32 a, i32 b) {
    return a > b ? a :b;
}
extern f64 pick_larger__f64(f64 a, f64 b) __asm__("_pick::larger(double)");
[[gnu::weak]][[gnu::visibility("hidden")]] f64 pick_larger__f64(f64 a, f64 b) {
    return a > b ? a :b;
}
extern __SIZE_TYPE__ pick_size_of_value__u8(u8 value) __asm__("_pick::size_of_value(unsigned char)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ pick_size_of_value__u8(u8 value) {
    (void) value;
    return sizeof(u8);
}
extern i32 pick_first_of__i32(const i32 * values, __SIZE_TYPE__ count) __asm__("_pick::first_of(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] i32 pick_first_of__i32(const i32 * values, __SIZE_TYPE__ count) {
    return count > 0 ? values[0] :(i32) {
        0 }
    ;
}
extern __SIZE_TYPE__ pick_size_of_value__unsigned_short(unsigned short value) __asm__("_pick::size_of_value(unsigned short)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ pick_size_of_value__unsigned_short(unsigned short value) {
    (void) value;
    return sizeof(unsigned short);
}
extern void pick_set__i32(i32 * out, i32 value) __asm__("_pick::set(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] void pick_set__i32(i32 * out, i32 value) {
    * out = value;
}
extern __INT32_TYPE__ pick_twice__i32(__INT32_TYPE__ value) __asm__("_pick::twice(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ pick_twice__i32(__INT32_TYPE__ value) {
    return
    __extension__({ extern i32 pick_larger__i32(i32 a, i32 b) __asm__("_pick::larger(int)"); pick_larger__i32(value, value); });
}

__extension__({ extern __INT32_TYPE__ kind_describe__F32(float value) __asm__("_kind::describe(float)"); kind_describe__F32(f); })
__extension__({ extern __INT32_TYPE__ kind_describe__U64(__UINT64_TYPE__ value) __asm__("_kind::describe(unsigned long long)"); kind_describe__U64(big); })
__extension__({ extern __INT32_TYPE__ kind_describe__I32_P(__INT32_TYPE__ * value) __asm__("_kind::describe(int *)"); kind_describe__I32_P(& small); })
__extension__({ extern __INT32_TYPE__ kind_describe__U8(__UINT8_TYPE__ value) __asm__("_kind::describe(unsigned char)"); kind_describe__U8(byte); })
__extension__({ extern __INT32_TYPE__ kind_describe__Point(Point value) __asm__("_kind::describe(Point)"); kind_describe__Point(p); })
__extension__({ extern __INT32_TYPE__ kind_describe__I32(__INT32_TYPE__ value) __asm__("_kind::describe(int)"); kind_describe__I32(small); })
__extension__({ extern __INT32_TYPE__ kind_floats_only__F32(float value) __asm__("_kind::floats_only(float)"); kind_floats_only__F32(f); })
__extension__({ extern __INT32_TYPE__ kind_floats_only__I32(__INT32_TYPE__ value) __asm__("_kind::floats_only(int)"); kind_floats_only__I32(small); })
__extension__({ extern __INT32_TYPE__ kind_bytes__U8(void) __asm__("_kind::bytes(unsigned char)"); kind_bytes__U8(); })
__extension__({ extern __INT32_TYPE__ kind_bytes__U64(void) __asm__("_kind::bytes(unsigned long long)"); kind_bytes__U64(); })
__extension__({ extern __INT32_TYPE__ kind_bytes__Point(void) __asm__("_kind::bytes(Point)"); kind_bytes__Point(); })
extern __INT32_TYPE__ kind_describe__F32(float value) __asm__("_kind::describe(float)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_describe__F32(float value) {
    {
        return 1;
    }
}
extern __INT32_TYPE__ kind_describe__U64(__UINT64_TYPE__ value) __asm__("_kind::describe(unsigned long long)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_describe__U64(__UINT64_TYPE__ value) {
    {
        return 2;
    }
}
extern __INT32_TYPE__ kind_describe__I32_P(__INT32_TYPE__ * value) __asm__("_kind::describe(int *)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_describe__I32_P(__INT32_TYPE__ * value) {
    {
        return 3;
    }
}
extern __INT32_TYPE__ kind_describe__U8(__UINT8_TYPE__ value) __asm__("_kind::describe(unsigned char)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_describe__U8(__UINT8_TYPE__ value) {
    {
        return 5;
    }
}
extern __INT32_TYPE__ kind_describe__Point(Point value) __asm__("_kind::describe(Point)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_describe__Point(Point value) {
    {
        return value.x + value.y;
    }
}
extern __INT32_TYPE__ kind_describe__I32(__INT32_TYPE__ value) __asm__("_kind::describe(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_describe__I32(__INT32_TYPE__ value) {
    {
        return 4;
        return 5;
    }
}
extern __INT32_TYPE__ kind_floats_only__F32(float value) __asm__("_kind::floats_only(float)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_floats_only__F32(float value) {
    {
        return(__INT32_TYPE__)(value * 2) + sizeof(float);
    }
    return 0;
}
extern __INT32_TYPE__ kind_floats_only__I32(__INT32_TYPE__ value) __asm__("_kind::floats_only(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_floats_only__I32(__INT32_TYPE__ value) {
    return 0;
}
extern __INT32_TYPE__ kind_bytes__U8(void) __asm__("_kind::bytes(unsigned char)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_bytes__U8(void) {
    return 2;
    return 3 + (sizeof(__UINT8_TYPE__) != 8);
}
extern __INT32_TYPE__ kind_bytes__U64(void) __asm__("_kind::bytes(unsigned long long)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_bytes__U64(void) {
    return 1;
    return 3 + (sizeof(__UINT64_TYPE__) != 8);
}
extern __INT32_TYPE__ kind_bytes__Point(void) __asm__("_kind::bytes(Point)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ kind_bytes__Point(void) {
    return 3 + (sizeof(Point) != 8);
}

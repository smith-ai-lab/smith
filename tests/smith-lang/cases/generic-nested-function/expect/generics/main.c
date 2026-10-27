__extension__({ extern void later_defer__show_int_i64(later_Slot * slot, [[maybe_unused]] int rest_0, [[maybe_unused]] i64 rest_1) __asm__("_later::defer(main_show, int, long long)@src/main.smith"); later_defer__show_int_i64(& a, 3, (__INT64_TYPE__) 4); })
__extension__({ extern void later_defer__scale_f64(later_Slot * slot, [[maybe_unused]] double rest_0) __asm__("_later::defer(main_scale, double)@src/main.smith"); later_defer__scale_f64(& b, 2.5); })
__extension__({ extern void later_defer__show_int_i64(later_Slot * slot, [[maybe_unused]] int rest_0, [[maybe_unused]] i64 rest_1) __asm__("_later::defer(main_show, int, long long)@src/main.smith"); later_defer__show_int_i64(& c, 5, (__INT64_TYPE__) 6); })
__extension__({ extern __INT32_TYPE__ later_doubled__i32(__INT32_TYPE__ x) __asm__("_later::doubled(int)"); later_doubled__i32(21); })
__extension__({ extern float later_doubled__f32(float x) __asm__("_later::doubled(float)"); later_doubled__f32(1.5f); })
__extension__({ extern __INT32_TYPE__ later_pair__int_long(int a, long b) __asm__("_later::pair(int, long)"); later_pair__int_long(1, 2L); })
struct smith_pack_later_defer__show_int_i64 {
    int _0;
    i64 _1;
}
;
static void later_defer__show_int_i64__entry(void * raw) {
    struct smith_pack_later_defer__show_int_i64 * a = raw;
    main_show(a->_0, a->_1);
}
extern void later_defer__show_int_i64(later_Slot * slot, [[maybe_unused]] int rest_0, [[maybe_unused]] i64 rest_1) __asm__("_later::defer(main_show, int, long long)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] void later_defer__show_int_i64(later_Slot * slot, [[maybe_unused]] int rest_0, [[maybe_unused]] i64 rest_1) {
    static_assert(sizeof(struct smith_pack_later_defer__show_int_i64) <= sizeof slot->data, "defer: too many arguments");
    struct smith_pack_later_defer__show_int_i64 args = {
        rest_0, rest_1 }
    ;
    * (struct smith_pack_later_defer__show_int_i64 *) slot->data = args;
    slot->entry = later_defer__show_int_i64__entry;
}
struct smith_pack_later_defer__scale_f64 {
    double _0;
}
;
static void later_defer__scale_f64__entry(void * raw) {
    struct smith_pack_later_defer__scale_f64 * a = raw;
    main_scale(a->_0);
}
extern void later_defer__scale_f64(later_Slot * slot, [[maybe_unused]] double rest_0) __asm__("_later::defer(main_scale, double)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] void later_defer__scale_f64(later_Slot * slot, [[maybe_unused]] double rest_0) {
    static_assert(sizeof(struct smith_pack_later_defer__scale_f64) <= sizeof slot->data, "defer: too many arguments");
    struct smith_pack_later_defer__scale_f64 args = {
        rest_0 }
    ;
    * (struct smith_pack_later_defer__scale_f64 *) slot->data = args;
    slot->entry = later_defer__scale_f64__entry;
}
static __INT32_TYPE__ later_doubled__i32__twice(__INT32_TYPE__ v) {
    {
        return v + v;
    }
}
extern __INT32_TYPE__ later_doubled__i32(__INT32_TYPE__ x) __asm__("_later::doubled(int)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ later_doubled__i32(__INT32_TYPE__ x) {
    return later_doubled__i32__twice(x);
}
static float later_doubled__f32__twice(float v) {
    {
        return v * 2.0f;
    }
}
extern float later_doubled__f32(float x) __asm__("_later::doubled(float)");
[[gnu::weak]][[gnu::visibility("hidden")]] float later_doubled__f32(float x) {
    return later_doubled__f32__twice(x);
}
static __INT32_TYPE__ later_pair__int_long__first(void) {
    return(__INT32_TYPE__) sizeof(int);
}
static __INT32_TYPE__ later_pair__int_long__second(void) {
    return(__INT32_TYPE__) sizeof(long);
}
extern __INT32_TYPE__ later_pair__int_long(int a, long b) __asm__("_later::pair(int, long)");
[[gnu::weak]][[gnu::visibility("hidden")]] __INT32_TYPE__ later_pair__int_long(int a, long b) {
    return later_pair__int_long__first() * 10 + later_pair__int_long__second();
}

__extension__({ extern void later_defer__show_int_i64(later_Slot * slot, [[maybe_unused]] int rest_0, [[maybe_unused]] i64 rest_1) __asm__("_later::defer(main_show, int, long)@src/main.smith"); later_defer__show_int_i64(& a, 3, (__INT64_TYPE__) 4); })
__extension__({ extern void later_defer__scale_f64(later_Slot * slot, [[maybe_unused]] double rest_0) __asm__("_later::defer(main_scale, double)@src/main.smith"); later_defer__scale_f64(& b, 2.5); })
__extension__({ extern void later_defer__show_int_i64(later_Slot * slot, [[maybe_unused]] int rest_0, [[maybe_unused]] i64 rest_1) __asm__("_later::defer(main_show, int, long)@src/main.smith"); later_defer__show_int_i64(& c, 5, (__INT64_TYPE__) 6); })
struct smith_pack_later_defer__show_int_i64 {
    int _0;
    i64 _1;
}
;
static void later_defer__show_int_i64__lambda0(void * raw) {
    struct smith_pack_later_defer__show_int_i64 * a = raw;
    main_show(a->_0, a->_1);
}
extern void later_defer__show_int_i64(later_Slot * slot, [[maybe_unused]] int rest_0, [[maybe_unused]] i64 rest_1) __asm__("_later::defer(main_show, int, long)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] void later_defer__show_int_i64(later_Slot * slot, [[maybe_unused]] int rest_0, [[maybe_unused]] i64 rest_1) {
    static_assert(sizeof(struct smith_pack_later_defer__show_int_i64) <= sizeof slot->data, "defer: too many arguments");
    struct smith_pack_later_defer__show_int_i64 args = {
        rest_0, rest_1 }
    ;
    * (struct smith_pack_later_defer__show_int_i64 *) slot->data = args;
    slot->entry = later_defer__show_int_i64__lambda0;
}
struct smith_pack_later_defer__scale_f64 {
    double _0;
}
;
static void later_defer__scale_f64__lambda0(void * raw) {
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
    slot->entry = later_defer__scale_f64__lambda0;
}

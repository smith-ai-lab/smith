#ifdef WIDE
typedef struct {
__UINT64_TYPE__ value;
} cfg_mode_Cell;
#else
typedef struct {
__UINT32_TYPE__ value;
} cfg_mode_Cell;
#endif
#if defined(WIDE) && WIDE > 1
__UINT32_TYPE__ cfg_mode_width([[maybe_unused]] __INT32_TYPE__ n) __asm__("_cfg::mode::width");
__UINT32_TYPE__ cfg_mode_width([[maybe_unused]] __INT32_TYPE__ n) {
return 128;
}
#elif defined(WIDE)
__UINT32_TYPE__ cfg_mode_width([[maybe_unused]] __INT32_TYPE__ n) __asm__("_cfg::mode::width");
__UINT32_TYPE__ cfg_mode_width([[maybe_unused]] __INT32_TYPE__ n) {
return 64;
}
#else
__UINT32_TYPE__ cfg_mode_width([[maybe_unused]] __INT32_TYPE__ n) __asm__("_cfg::mode::width");
__UINT32_TYPE__ cfg_mode_width([[maybe_unused]] __INT32_TYPE__ n) {
return 32;
}
#endif
#ifdef UNUSED
static __UINT32_TYPE__ cfg_mode_hidden = 1;
#endif
__UINT32_TYPE__ cfg_mode_always([[maybe_unused]] __INT32_TYPE__ n) __asm__("_cfg::mode::always");
__UINT32_TYPE__ cfg_mode_always([[maybe_unused]] __INT32_TYPE__ n) {
return 1;
}
#ifdef OUTER
#ifdef INNER
__UINT32_TYPE__ cfg_mode_nested() __asm__("_cfg::mode::nested");
__UINT32_TYPE__ cfg_mode_nested() {
return 3;
}
#endif
#endif
__UINT32_TYPE__ cfg_mode_plain([[maybe_unused]] __INT32_TYPE__ n) __asm__("_cfg::mode::plain");
__UINT32_TYPE__ cfg_mode_plain([[maybe_unused]] __INT32_TYPE__ n) {
return 5;
}

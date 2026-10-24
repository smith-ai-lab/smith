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
__UINT32_TYPE__ cfg_mode_width() __asm__("_cfg::mode::width");
__UINT32_TYPE__ cfg_mode_width() {
return 128;
}
#elif defined(WIDE)
__UINT32_TYPE__ cfg_mode_width() __asm__("_cfg::mode::width");
__UINT32_TYPE__ cfg_mode_width() {
return 64;
}
#else
__UINT32_TYPE__ cfg_mode_width() __asm__("_cfg::mode::width");
__UINT32_TYPE__ cfg_mode_width() {
return 32;
}
#endif
#ifdef UNUSED
static __UINT32_TYPE__ cfg_mode_hidden = 1;
#endif
__UINT32_TYPE__ cfg_mode_always() __asm__("_cfg::mode::always");
__UINT32_TYPE__ cfg_mode_always() {
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
__UINT32_TYPE__ cfg_mode_plain() __asm__("_cfg::mode::plain");
__UINT32_TYPE__ cfg_mode_plain() {
return 5;
}

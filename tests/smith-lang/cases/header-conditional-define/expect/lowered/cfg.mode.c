typedef struct {
__UINT64_TYPE__ value;
} cfg_mode_Cell;
typedef struct {
__UINT32_TYPE__ value;
} cfg_mode_Cell;
__UINT32_TYPE__ cfg_mode_width(void) __asm__("_cfg::mode::width");
__UINT32_TYPE__ cfg_mode_width(void) {
return 128;
}
__UINT32_TYPE__ cfg_mode_width(void) __asm__("_cfg::mode::width");
__UINT32_TYPE__ cfg_mode_width(void) {
return 64;
}
__UINT32_TYPE__ cfg_mode_width(void) __asm__("_cfg::mode::width");
__UINT32_TYPE__ cfg_mode_width(void) {
return 32;
}
static __UINT32_TYPE__ cfg_mode_hidden = 1;
__UINT32_TYPE__ cfg_mode_always(void) __asm__("_cfg::mode::always");
__UINT32_TYPE__ cfg_mode_always(void) {
return 1;
}
__UINT32_TYPE__ cfg_mode_nested(void) __asm__("_cfg::mode::nested");
__UINT32_TYPE__ cfg_mode_nested(void) {
return 3;
}
__UINT32_TYPE__ cfg_mode_plain(void) __asm__("_cfg::mode::plain");
__UINT32_TYPE__ cfg_mode_plain(void) {
return 5;
}

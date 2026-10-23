typedef struct {
__INT64_TYPE__ total;
__UINT32_TYPE__ count;
} store__Tally;
typedef __UINT8_TYPE__ store__Mode;
constexpr store__Mode store__mode_add = 0;
constexpr store__Mode store__mode_scale = 1;
enum store__Size { store__size_small = 1, store__size_large = 10 };
static __INT64_TYPE__ store_weight = 3; __attribute__((visibility("hidden"))) store__Tally store__start(void); __attribute__((visibility("hidden"))) store__Tally store__start(void) {
return (store__Tally){0, 0};
} __attribute__((visibility("hidden"))) store__Tally store__step(store__Tally tally, __INT64_TYPE__ value, store__Mode mode); __attribute__((visibility("hidden"))) store__Tally store__step(store__Tally tally, __INT64_TYPE__ value, store__Mode mode) {
if (mode == store__mode_scale) {
value = value * store_weight;
}
tally.total = tally.total + value;
tally.count = tally.count + 1;
return tally;
}
static __INT64_TYPE__ store_unused(__INT64_TYPE__ value) {
return value + store_weight;
} [ [ clang :: unavailable ( "store::twice is a generic function: call it from a .smith module" ) ] ] void store__twice ( void ) ; [ [ clang :: unavailable ( "store::total is a generic function: call it from a .smith module" ) ] ] void store_total ( void ) ;
__INT64_TYPE__ store_current_weight(void) __asm__("_store::current_weight");
__INT64_TYPE__ store_current_weight(void) {
return store_unused(0);
}

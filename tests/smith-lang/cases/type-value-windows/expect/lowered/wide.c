static constexpr type wide_wide = long; [ [ clang :: unavailable ( "wide::long_is_four is a function over `type`: it exists only at compile time" ) ] ] void wide_long_is_four ( void ) ;
static_assert(!wide_long_is_four(wide_wide), "long is four bytes here");
static_assert(sizeof(wide_wide) != 4, "sizeof of a type value is four bytes here");
static_assert(alignof(wide_wide) != 4, "alignof of a type value is four here");
static_assert(sizeof(char *) == 8);
__INT32_TYPE__ wide_eight() __asm__("_wide::eight");
__INT32_TYPE__ wide_eight() {
return 8;
}

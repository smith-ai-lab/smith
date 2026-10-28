static constexpr Type wide_Wide = long; [ [ clang :: unavailable ( "wide::long_is_four is a function over `Type`: it exists only at compile time" ) ] ] void wide_long_is_four ( void ) ;
static_assert(!wide_long_is_four(wide_Wide), "long is four bytes here");
static_assert(sizeof(wide_Wide) != 4, "sizeof of a type value is four bytes here");
static_assert(alignof(wide_Wide) != 4, "alignof of a type value is four here");
static_assert(sizeof(char *) == 8);
__INT32_TYPE__ wide_eight() __asm__("_wide::eight");
__INT32_TYPE__ wide_eight() {
return 8;
}

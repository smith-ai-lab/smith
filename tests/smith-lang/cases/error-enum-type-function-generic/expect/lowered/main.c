[ [ clang :: unavailable ( "main::first is a generic function: call it from a .smith module" ) ] ] void main_first ( void ) ;
__INT32_TYPE__ main(void) {
__SIZE_TYPE__ xs[1] = {1};__extension__({extern struct smith_enum_maybe_usize main_first__Usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_main::first(__SIZE_TYPE__)");main_first__Usize(xs,1);});
return 0;
}
struct smith_enum_maybe_usize{enum smith_enum_maybe_usize_tag{smith_enum_maybe_usize_tag_some,smith_enum_maybe_usize_tag_none}tag;union{__SIZE_TYPE__ some;};}__attribute__((annotate("std::enum")));;extern struct smith_enum_maybe_usize main_first__Usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n)__asm__("_main::first(__SIZE_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_enum_maybe_usize main_first__Usize(__SIZE_TYPE__*xs,__SIZE_TYPE__ n){if(n==0)return.none;return{.some=xs[0]};}

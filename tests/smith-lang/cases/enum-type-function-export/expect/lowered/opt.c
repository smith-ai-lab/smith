struct smith_enum_maybe_usize {enum smith_enum_maybe_usize_tag {smith_enum_maybe_usize_tag_some, smith_enum_maybe_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_maybe_usize opt_Index; [ [ clang :: unavailable ( "opt::first is a generic function: call it from a .smith module" ) ] ] void opt_first ( void ) ;
struct smith_enum_maybe_usize opt_find(const char *text, char wanted) __asm__("_opt::find");
struct smith_enum_maybe_usize opt_find(const char *text, char wanted) {
for (__SIZE_TYPE__ i = 0; text[i] != 0; i++)
if (text[i] == wanted) return {.some = i};
return .none;
}

struct smith_enum_option_usize {enum smith_enum_option_usize_tag {smith_enum_option_usize_tag_some, smith_enum_option_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("std::enum")));
struct smith_enum_option_usize finder_find(const char *text, char wanted) __asm__("_finder::find");
struct smith_enum_option_usize finder_find(const char *text, char wanted) {
for (__SIZE_TYPE__ i = 0; text[i] != 0; i++)
if (text[i] == wanted) return {.some = i};
return .none;
}

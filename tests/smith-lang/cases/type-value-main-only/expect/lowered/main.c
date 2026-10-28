typedef struct smith_array_const_char_n { const char * data; __SIZE_TYPE__ count [1]; } Str;
__INT32_TYPE__ main() {
constexpr Type t = Str;
static_assert(Slice(t));
t words = "abc";
return (__INT32_TYPE__)countof(words) - 3;
}

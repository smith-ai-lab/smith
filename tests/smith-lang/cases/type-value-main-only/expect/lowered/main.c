typedef struct smith_array_const_char_n { const char * data; __SIZE_TYPE__ count [1]; } str;
__INT32_TYPE__ main() {
constexpr Type T = str;
static_assert(Slice(T));
T words = "abc";
return (__INT32_TYPE__)countof(words) - 3;
}

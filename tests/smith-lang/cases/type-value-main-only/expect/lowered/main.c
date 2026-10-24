typedef struct smith_array_const_char_n { const char * data; __SIZE_TYPE__ count [1]; } str;
__INT32_TYPE__ main() {
constexpr type t = str;
static_assert(is_slice(t));
t words = "abc";
return (__INT32_TYPE__)countof(words) - 3;
}

constexpr char hello[] = "hello";
constexpr const char *word = "abc";
constexpr const char *outside = hello + 9;
static_assert(word[0] == 'z');
__INT32_TYPE__ main() {
const __INT32_TYPE__ automatic[2] = {1, 2};
constexpr const __INT32_TYPE__ *pointer = automatic;
return *pointer;
}

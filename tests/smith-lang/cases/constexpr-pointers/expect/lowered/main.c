#include <stdio.h>

typedef struct {
const char *ptr;
__INT32_TYPE__ len;
} Text;
constexpr char hello[] = "hello world";
constexpr __INT32_TYPE__ nums[4] = {10, 20, 30, 40};
constexpr const char *word = "abc";
constexpr const char *inside = &hello[1];
constexpr const __INT32_TYPE__ *third = nums + 2;
constexpr Text small = {"xy", 2};
constexpr Text prefix = {hello, 5};
static __INT32_TYPE__ length(const char *p) {
__INT32_TYPE__ n = 0;
while (p[n])
n++;
return n;
}
static_assert(word[1] == 'b');
static_assert(inside[0] == 'e');
static_assert(*third == 30);
static_assert(small.ptr[1] == 'y' && small.len == 2);
static_assert(prefix.ptr[4] == 'o');
static_assert(length(prefix.ptr) == 11);
__INT32_TYPE__ main() {
static constexpr const __INT32_TYPE__ *kept = nums;
printf("%c %d %s %d\n", word[0], *kept, small.ptr, length(inside));
return 0;
}

#include <stdio.h>

typedef int (*kit_things_Callback)(int value);
typedef union { __UINT32_TYPE__ whole; __UINT8_TYPE__ bytes[4]; } kit_things_Word;
typedef struct kit_things_Node {
struct kit_things_Node *next;
__INT32_TYPE__ value;
__UINT32_TYPE__ flag : 1;
__UINT32_TYPE__ mode : 3;
struct { __INT16_TYPE__ x, y; } point;
} kit_things_Node;
typedef struct kit_things_Node kit_things_Node;
constexpr __SIZE_TYPE__ kit_things_capacity = 4;
extern const char *const kit_things_names[] __asm__("_kit::things::names");
const char *const kit_things_names[] __asm__("_kit::things::names") = {"zero", "one", "two"};
static_assert(sizeof(kit_things_Word) == 4, "a word is four bytes");
[[nodiscard]] static inline int kit_things_square(int value) {
return value * value;
}
static int kit_things_apply_twice(kit_things_Callback callback, int value) {
return callback(callback(value));
}
int kit_things_run(kit_things_Callback callback, int value) __asm__("_kit::things::run");
int kit_things_run(kit_things_Callback callback, int value) {
return kit_things_apply_twice(callback, value);
}
int kit_things_sum(const kit_things_Node *node) __asm__("_kit::things::sum");
int kit_things_sum(const kit_things_Node *node) {
int total = 0;
for (; node != nullptr; node = node->next) {
total += node->value + node->point.x + (int)node->mode;
}
return total;
}
kit_things_Word kit_things_make_word(__UINT32_TYPE__ whole) __asm__("_kit::things::make_word");
kit_things_Word kit_things_make_word(__UINT32_TYPE__ whole) {
kit_things_Word word = {.whole = whole};
return word;
}
int kit_things_by_type(int value) __asm__("_kit::things::by_type");
int kit_things_by_type(int value) {
return _Generic(value, int: kit_things_square(value), default: 0);
}
__UINT32_TYPE__ kit_things_pick(__UINT32_TYPE__ index) __asm__("_kit::things::pick");
__UINT32_TYPE__ kit_things_pick(__UINT32_TYPE__ index) {
static const __UINT32_TYPE__ table[] = {10, 20, 30, 40};
typeof(index) clamped = index < kit_things_capacity ? index : kit_things_capacity - 1;
return table[clamped];
}
int kit_things_say(const char *text) __asm__("_kit::things::say");
int kit_things_say(const char *text) {
return puts(text);
}
typedef int kit_things_Local;
kit_things_Local kit_things_twice_local(kit_things_Local value) __asm__("_kit::things::twice_local");
kit_things_Local kit_things_twice_local(kit_things_Local value) {
kit_things_Local result = value * 2;
return result;
}
#define TRIPLE(value) ((value) * 3)
int kit_things_triple(int value) __asm__("_kit::things::triple");
int kit_things_triple(int value) {
return TRIPLE(value);
}

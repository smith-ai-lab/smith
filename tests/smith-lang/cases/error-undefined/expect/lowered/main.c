static __INT32_TYPE__ main_global = undefined;
struct main_Pair {
__INT32_TYPE__ undefined;
__INT32_TYPE__ other;
};
typedef struct main_Pair main_Pair;
static __INT32_TYPE__ main_value(void) {
return undefined;
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ x __attribute__((uninitialized));
x = undefined;
__INT32_TYPE__ y = 1 + undefined;
__INT32_TYPE__ z = undefined, w = 2;
static __INT32_TYPE__ s = undefined;
return x + y + z + w + s + main_value();
}

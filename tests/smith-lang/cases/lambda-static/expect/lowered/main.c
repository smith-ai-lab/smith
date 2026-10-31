#include <stdio.h>

static __INT32_TYPE__ (*const square)(__INT32_TYPE__) =(__INT32_TYPE__ a) -> i32 { return a * a; };
struct main_Op {
const char *name;
__INT32_TYPE__ (*call)(__INT32_TYPE__, __INT32_TYPE__);
};
typedef struct main_Op main_Op;
static const struct main_Op main_ops[] = {
{"plus",(__INT32_TYPE__ a, __INT32_TYPE__ b) { return a + b; }},
{"minus",(__INT32_TYPE__ a, __INT32_TYPE__ b) { return a - b; }},
};
__INT32_TYPE__ main(void) {
printf("%d\n", square(7));
for (__INT32_TYPE__ i = 0; i < 2; i++) {
printf("%s %d\n", main_ops[i].name, main_ops[i].call(9, 4));
}
return 0;
}

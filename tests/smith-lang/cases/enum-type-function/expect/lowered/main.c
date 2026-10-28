#include <stdio.h>

typedef struct {
float radius;
} main_Circle;struct smith_enum_maybe_usize {enum smith_enum_maybe_usize_tag {smith_enum_maybe_usize_tag_some, smith_enum_maybe_usize_tag_none } tag; union { __SIZE_TYPE__ some; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_maybe_usize main_find(const char *text, char wanted) {
for (__SIZE_TYPE__ i = 0; text[i] != 0; i++)
if (text[i] == wanted) return {.some = i};
return .none;
}
static struct smith_enum_maybe_usize main_again(__SIZE_TYPE__ v) {
struct smith_enum_maybe_usize x = {.some = v};
return x;
}struct smith_enum_outcome_main_Circle__i32 {enum smith_enum_outcome_main_Circle__i32_tag {smith_enum_outcome_main_Circle__i32_tag_ok, smith_enum_outcome_main_Circle__i32_tag_error } tag; union { main_Circle ok; __INT32_TYPE__ error; }; } __attribute__((annotate("smith::enum")));
static struct smith_enum_outcome_main_Circle__i32 main_make(Bool good) {
if (good) return {.ok = {.radius = 2}};
return {.error = 7};
}struct smith_enum_maybe_main_Circle_ptr {enum smith_enum_maybe_main_Circle_ptr_tag {smith_enum_maybe_main_Circle_ptr_tag_some, smith_enum_maybe_main_Circle_ptr_tag_none } tag; union { main_Circle * some; }; } __attribute__((annotate("smith::enum")));
typedef struct smith_enum_maybe_main_Circle_ptr main_MaybeCircle;
static float main_radius_of(main_MaybeCircle c) {
if (c == .some) return c.some->radius;
return -1;
}
static __SIZE_TYPE__ main_unwrap(struct smith_enum_maybe_usize o, __SIZE_TYPE__ otherwise) {
if (o == .some) return o.some;
return otherwise;
}
__INT32_TYPE__ main(void) {
struct smith_enum_maybe_usize at = main_find("key=value", '=');
struct smith_enum_maybe_usize missing = main_find("none", '=');
struct smith_enum_outcome_main_Circle__i32 good = main_make(true);
struct smith_enum_outcome_main_Circle__i32 bad = main_make(false);
printf("%zu %zu\n", main_unwrap(at, 99), main_unwrap(missing, 99));
printf("%zu\n", main_unwrap(main_again(5), 0));
main_Circle unit = {.radius = 3};
main_MaybeCircle held = {.some = &unit};
printf("%.0f %.0f\n", (double)main_radius_of(held), (double)main_radius_of(.none));
switch (good) {
case .ok: printf("ok %.0f\n", (double)good.ok.radius); break;
case .error: printf("error %d\n", good.error); break;
}
switch (bad) {
case .ok: printf("ok\n"); break;
case .error: printf("error %d\n", bad.error); break;
}
return 0;
}

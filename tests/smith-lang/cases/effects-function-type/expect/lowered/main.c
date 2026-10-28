#include <stdio.h>

typedef void (*main_Logger)(const char *text) [[clang::annotate_type("smith::effects:console")]];
struct main_Sink {
main_Logger log;
__INT32_TYPE__ (*step)(__INT32_TYPE__ x) [[clang::annotate_type("smith::effects:")]];
};
typedef struct main_Sink main_Sink;
[[smith::effects(console)]] static void main_say(const char *text) {
printf("%s\n", text);
}
static __INT32_TYPE__ main_twice(__INT32_TYPE__ x) {
return x * 2;
}
__INT32_TYPE__ main_apply(__INT32_TYPE__ (*f)(__INT32_TYPE__ x) [[clang::annotate_type("smith::effects:")]], __INT32_TYPE__ x) __asm__("_main::apply");
__INT32_TYPE__ main_apply(__INT32_TYPE__ (*f)(__INT32_TYPE__ x) [[clang::annotate_type("smith::effects:")]], __INT32_TYPE__ x) {
return f(x);
}
__INT32_TYPE__ main(void) {
main_Sink sink = {.log = main_say, .step = main_twice};
void (*other)(const char *text) [[clang::annotate_type("smith::effects:console,files")]] = main_say;
sink.log("hello");
other("world");
printf("%d\n", main_apply(sink.step, 21));
return 0;
}

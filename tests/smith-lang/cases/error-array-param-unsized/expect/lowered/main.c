static __INT32_TYPE__ main_first(__INT32_TYPE__ xs[]) {
return xs[0];
}
__INT32_TYPE__ main_second(const char *names[]) __asm__("_main::second");
__INT32_TYPE__ main_second(const char *names[]) {
return names[0][0];
}
static __INT32_TYPE__ main_mine(__INT32_TYPE__ argc, char *argv[]) {
__INT32_TYPE__ xs[1] = {};
return main_first(xs) + argc;
}

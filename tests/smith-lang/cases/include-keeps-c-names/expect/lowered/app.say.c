#include <stdio.h>
void app_say_hello(const char *name) __asm__("_app::say::hello");
void app_say_hello(const char *name) {
printf("hi %s\n", name);
}

#include <stdio.h>

int uses_macro_end_bad() __asm__("_uses::macro::end_bad");
int uses_macro_end_bad() {
return EOF;
}
int uses_macro_end_ok() __asm__("_uses::macro::end_ok");
int uses_macro_end_ok() {
return EOF;
}

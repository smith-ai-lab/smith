#include <stdio.h>

static char main_first(const char *text) {
return text[0];
}
__INT32_TYPE__ main(void) {
char letter = 'x';
const char *word = "hello";
printf("%c %c\n", letter, main_first(word));
return 0;
}

#include <stdio.h>
int main(void) {
lib_text_Text t = lib_text_make("hello");
printf("%zu %d\n", lib_text_length(t), lib_util_twice(21));
return 0;
}

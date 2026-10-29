#include "lib/text.h"
#include "lib/gnu.h"
#include <stdio.h>

int main() {
lib_text_Text text = lib_text_make("shadow");
int c = 2;
[[gnu::unused]] int unused_one = lib_gnu_answer(0);
int gnu = unused_one;
printf("%zu %d %d\n", lib_text_length(text), c, gnu);
return 0;
}

#include <stdio.h>
struct Holder {
const char *str;
};
static int measure(const char *str) {
int n = 0;
while (str[n] != 0) n++;
return n;
}
int main() {
const char *str = "abcd";
struct Holder holder = {.str = str};
printf("%d %d\n", measure(str), measure(holder.str));
return 0;
}

#include <stdio.h>

struct [[maybe_unused]] main_Plain {
__INT32_TYPE__ value;
};
typedef struct main_Plain main_Plain;
struct [[clang::annotate("main::capability")]] MainCounter {
__INT32_TYPE__ count;
};
typedef struct MainCounter MainCounter;
union [[maybe_unused]] main_Number {
__INT32_TYPE__ whole;
float real;
};
typedef union main_Number main_Number;
__INT32_TYPE__ main(void) {
main_Plain plain = {.value = 4};
MainCounter counter = {.count = 5};
main_Number number = {.whole = 6};
printf("%d %d %d\n", plain.value, counter.count, number.whole);
return 0;
}

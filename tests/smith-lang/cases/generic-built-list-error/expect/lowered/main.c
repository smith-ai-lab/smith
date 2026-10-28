#include "bad.h"
#include "plain.h"

int main(void) {
__INT32_TYPE__ a =smith_call_failed;
__INT32_TYPE__ b =smith_call_failed;
__INT32_TYPE__ p = plain_not_a_list(1);
return a + b + p;
}

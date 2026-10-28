__INT32_TYPE__ plain_not_generic(__INT32_TYPE__ x) __asm__("_plain::not_generic");
__INT32_TYPE__ plain_not_generic(__INT32_TYPE__ x) {
for (__SIZE_TYPE__ i = 0; i < 2; i++) {
x += 1;
}
return x;
}

__UINT32_TYPE__ plain(void) {
__UINT32_TYPE__ xs[] = {1, 2, 3};
return (__UINT32_TYPE__)countof(xs);
}
__INT32_TYPE__ main(void) {
return (__INT32_TYPE__)plain() - 3;
}

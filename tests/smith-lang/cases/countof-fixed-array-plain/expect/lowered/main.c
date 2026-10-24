__UINT32_TYPE__ plain() {
__UINT32_TYPE__ xs[] = {1, 2, 3};
return (__UINT32_TYPE__)countof(xs);
}
__INT32_TYPE__ main() {
return (__INT32_TYPE__)plain() - 3;
}

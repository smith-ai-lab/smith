static __UINT32_TYPE__ main_bad(__UINT32_TYPE__ *p) {
return (__UINT32_TYPE__)countof(p);
}
__INT32_TYPE__ main(void) {
__UINT32_TYPE__ x = 0;
return (__INT32_TYPE__)main_bad(&x);
}

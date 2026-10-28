static __SIZE_TYPE__ main_bytes(__INT32_TYPE__ xs[static 2]) {
return sizeof(xs);
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ xs[2] = {};
return (__INT32_TYPE__)main_bytes(xs);
}

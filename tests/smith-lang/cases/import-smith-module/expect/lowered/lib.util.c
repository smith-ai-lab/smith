int lib_util_twice(int value) __asm__("_lib::util::twice");
int lib_util_twice(int value) {
return value * 2;
}

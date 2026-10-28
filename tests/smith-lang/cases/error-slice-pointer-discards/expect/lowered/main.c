static void main_touch(struct smith_array_char_n {char * data; __SIZE_TYPE__ count [1]; } *xs) {
(*xs)[0] = 'x';
}
__INT32_TYPE__ main(void) {struct smith_array_const_char_n {
const char * data; __SIZE_TYPE__ count [1]; } s = "abc";
main_touch(&s);
return 0;
}

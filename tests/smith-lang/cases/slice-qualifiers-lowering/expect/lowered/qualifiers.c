typedef struct {struct smith_array_const_char_n {
const char * data; __SIZE_TYPE__ count [1]; } name;struct smith_array_restrict_u8_n {
__UINT8_TYPE__ * __restrict data; __SIZE_TYPE__ count [1]; } bytes;
} qualifiers_Entry;
struct smith_array_const_u8_n {const __UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; };
__SIZE_TYPE__ qualifiers_total(struct smith_array_const_u8_n xs) __asm__("_qualifiers::total");
__SIZE_TYPE__ qualifiers_total(struct smith_array_const_u8_n xs) {
__SIZE_TYPE__ sum = 0;
for (__SIZE_TYPE__ i = 0; i < countof(xs); i++) {
sum += xs[i];
}
return sum;
}
struct smith_array_restrict_const_u8_n { __UINT8_TYPE__ const * __restrict data; __SIZE_TYPE__ count [1]; };
void qualifiers_copy(struct smith_array_restrict_u8_n dst,const struct smith_array_restrict_const_u8_n src) __asm__("_qualifiers::copy");
void qualifiers_copy(struct smith_array_restrict_u8_n dst,const struct smith_array_restrict_const_u8_n src) {
for (__SIZE_TYPE__ i = 0; i < countof(dst) && i < countof(src); i++) {
dst[i] = src[i];
}
}
__SIZE_TYPE__ qualifiers_greet(void) __asm__("_qualifiers::greet");
__SIZE_TYPE__ qualifiers_greet(void) {struct smith_array_const_char_n word = "abc";
__UINT8_TYPE__ bytes[3] = {1, 2, 3};const struct smith_array_u8_n {
__UINT8_TYPE__ * data; __SIZE_TYPE__ count [1]; } writable = bytes;struct smith_array_const_u8_n view = writable[1:];
return countof(word) + qualifiers_total(view) + qualifiers_total(writable);
}

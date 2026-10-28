struct words_Pair {
struct smith_array_const_char_n { const char * data; __SIZE_TYPE__ count [1]; } key;
struct smith_array_const_char_n value;
};
typedef struct words_Pair words_Pair;
__SIZE_TYPE__ words_total(struct smith_array_const_char_n xs) __asm__("_words::total");
__SIZE_TYPE__ words_total(struct smith_array_const_char_n xs) {
return countof(xs);
}
struct smith_array_const_char_n words_first_word(struct smith_array_const_char_n xs) __asm__("_words::first_word");
struct smith_array_const_char_n words_first_word(struct smith_array_const_char_n xs) {
__SIZE_TYPE__ n = 0;
while (n < countof(xs) && xs[n] != ' ') n++;
return xs[0:n];
}
Bool words_pop(struct smith_array_const_char_n *p) __asm__("_words::pop");
Bool words_pop(struct smith_array_const_char_n *p) {
if (countof(*p) == 0) return false;
*p = (*p)[1:];
return true;
}

typedef struct smith_array_const_char_n { const char * data; __SIZE_TYPE__ count [1]; } text_Str;
struct text_Pair {
text_Str key;
text_Str value;
};
typedef struct text_Pair text_Pair;
__SIZE_TYPE__ text_total(text_Str xs) __asm__("_text::total");
__SIZE_TYPE__ text_total(text_Str xs) {
return countof(xs);
}
text_Str text_first_word(text_Str xs) __asm__("_text::first_word");
text_Str text_first_word(text_Str xs) {
__SIZE_TYPE__ n = 0;
while (n < countof(xs) && xs[n] != ' ') n++;
return xs[0:n];
}
bool text_pop(text_Str *p) __asm__("_text::pop");
bool text_pop(text_Str *p) {
if (countof(*p) == 0) return false;
*p = (*p)[1:];
return true;
} [ [ clang :: unavailable ( "text::count_of is a generic function: call it from a .smith module" ) ] ] void text_count_of ( void ) ;

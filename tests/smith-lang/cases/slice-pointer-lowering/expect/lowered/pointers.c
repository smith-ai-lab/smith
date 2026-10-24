typedef struct smith_array_const_char_n {const char * data; __SIZE_TYPE__ count [1]; } pointers_Text;
bool pointers_take_line(struct smith_array_const_char_n *text,struct smith_array_const_char_n *line) __asm__("_pointers::take_line");
bool pointers_take_line(struct smith_array_const_char_n *text,struct smith_array_const_char_n *line) {
*line = (*text)[0:1];
*text = (*text)[1:];
return countof(*text) > 0;
}
__SIZE_TYPE__ pointers_length(pointers_Text *text) __asm__("_pointers::length");
__SIZE_TYPE__ pointers_length(pointers_Text *text) {
return countof(*text);
}
void pointers_grow(struct smith_array_i32_n_2 {__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } *const xs) __asm__("_pointers::grow");
void pointers_grow(struct smith_array_i32_n_2 {__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } *const xs) {
(*xs)[0][1] = 1;
}

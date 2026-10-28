struct main_Pair {struct smith_array_const_char_n {
const char * data; __SIZE_TYPE__ count [1]; } key;struct smith_array_const_char_n value;
};
typedef struct main_Pair main_Pair;
struct main_Wrap {
struct main_Pair pair;struct smith_array_const_char_n note;
};
typedef struct main_Wrap main_Wrap;
__INT32_TYPE__ main(void) {
struct main_Pair a = {.key = {"k"}};
struct main_Wrap w = {.pair.key = "k"};
return (__INT32_TYPE__)(countof(a.key) + countof(w.note));
}

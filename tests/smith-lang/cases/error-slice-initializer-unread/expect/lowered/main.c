struct main_Pair {struct smith_array_const_char_n {
const char * data; __SIZE_TYPE__ count [1]; } key;
__INT32_TYPE__ id;
};
typedef struct main_Pair main_Pair;
struct main_Anon {
union {
__INT32_TYPE__ a;
float b;
};struct smith_array_const_char_n name;
};
typedef struct main_Anon main_Anon;
struct main_Long {struct smith_array_const_char_n name;
__INT32_TYPE__ values[130];
};
typedef struct main_Long main_Long;
struct main_Holder {
struct main_Anon inner;
};
typedef struct main_Holder main_Holder;
__INT32_TYPE__ main(void) {
struct main_Pair ps[2] = {{.key = "a"}, {.key = "b"}};
struct main_Anon an = {.name = "x"};
struct main_Holder h = {.inner = {.name = "y"}};
struct main_Long lg = {.name = "z", .values = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119}};
return ps[0].id + an.a + h.inner.a + lg.values[0];
}

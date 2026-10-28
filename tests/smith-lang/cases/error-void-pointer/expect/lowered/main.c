typedef struct main_Ball {
__INT32_TYPE__ size;
} main_Ball;
typedef struct main_Ball main_Ball;
static void *main_erase(main_Ball *ball) {
return ball;
}
static void main_take(main_Ball *ball) {
(void)ball;
}
static main_Ball *main_returned(void *data) {
return data;
}
static void main_positions(void *data) {
main_Ball *initialized = data;
main_Ball *assigned;
assigned = main_erase(initialized);
main_take(data);
main_take((main_Ball *)data);
main_take(nullptr);
}
__INT32_TYPE__ main(void) {
return 0;
}

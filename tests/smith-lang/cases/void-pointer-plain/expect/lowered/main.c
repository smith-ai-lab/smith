#include <stdio.h>

typedef struct Ball {
__INT32_TYPE__ size;
} Ball;
static void *erase(Ball *ball) {
return ball;
}
static Ball *unerase(void *data) {
return data;
}
__INT32_TYPE__ main(__INT32_TYPE__ argc, char *argv[]) {
Ball ball = {.size = 7};
Ball *a = erase(&ball);
Ball *b;
b = erase(&ball);
printf("%d %d %d\n", a->size, b->size, unerase(&ball)->size);
return 0;
}

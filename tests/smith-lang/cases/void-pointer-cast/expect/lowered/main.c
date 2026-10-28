#include <stdio.h>

typedef struct main_Ball {
__INT32_TYPE__ size;
} main_Ball;
typedef struct main_Ball main_Ball;
static __INT32_TYPE__ main_read_size(void *data) {
main_Ball *ball = (main_Ball *)data;
return ball->size;
}
static const main_Ball *main_view(const void *data) {
return (const main_Ball *)data;
}
static void *main_erase(main_Ball *ball) {
return ball;
}
static void main_take(void *data, const void *constant) {
(void)data;
(void)constant;
}
__INT32_TYPE__ main(void) {
main_Ball ball = {.size = 7};
void *data = &ball;
const void *constant = &ball;
main_take(&ball, &ball);
main_Ball *none = nullptr;
printf("%d %d %d %d\n", main_read_size(data), main_view(constant)->size, main_read_size(main_erase(&ball)), none == nullptr);
return 0;
}

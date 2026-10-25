#include <stdio.h>

typedef struct {
    int x;
    int y;
} Point;

static int add(Point p) {
    int sum = p.x + p.y;
    return sum;
}

int main(void) {
    Point p = {3, 4};
    int r = add(p);
    printf("%d\n", r);
    return r == 7 ? 0 : 1;
}

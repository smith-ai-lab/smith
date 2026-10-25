#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int x;
    int y;
} Point;

static int pick(Point p, int i) {
    int a[4] = {1, 2, 3, 4};
    int v = a[i & 3] + p.x;
    return v;
}

int main(int argc, char **argv) {
    Point p = {3, 4};
    printf("out\n");
    fprintf(stderr, "err\n");
    if (argc > 1 && argv[1][0] == 'n') return *(int *)(long)(argc - 2);
    if (argc > 1 && argv[1][0] == 'a') abort();
    if (argc > 1 && argv[1][0] == 'h') {
        int *heap = malloc(16);
        return heap[pick(p, 1) + 8];
    }
    if (argc > 1 && argv[1][0] == 'e') return 7;
    int total = 0;
    for (int i = 0; i < 3; i++)
        total += pick(p, i);
    return total == 0;
}

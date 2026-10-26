// A tag and an ordinary identifier of one name are separate in C, and in a file with no `module` line: a tag is a type
// name only in a module (issue #333).
#include <stdio.h>

struct Point {
    int x;
};
static int Point = 3;

struct stat {
    int size;
};
static int stat(int fd) {
    return fd + 1;
}

enum Color { red, green };
static int Color(int c) {
    return c * 2;
}

int main(void) {
    struct Point p = {.x = 4};
    struct stat s = {.size = 5};
    struct Local {
        int inner;
    };
    int Local = 6;
    struct Local l = {.inner = 7};
    printf("%d %d %d %d %d %d\n", p.x + Point, s.size + stat(1), (int)green + Color(2), Local, l.inner,
           (int)sizeof(struct Point));
    return 0;
}

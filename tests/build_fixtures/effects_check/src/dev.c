#include <proj/dev.h>

#include <stdio.h>

void dev_write(Dev *dev, const char *text) {
    (void)dev;
    fputs(text, stdout);
}

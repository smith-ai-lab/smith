#pragma once
typedef struct [[clang::annotate("smith::capability")]] {
    int fd;
} Dev;
void dev_write(Dev *dev, const char *text);

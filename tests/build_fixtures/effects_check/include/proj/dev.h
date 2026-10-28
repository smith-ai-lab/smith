#pragma once
typedef struct [[clang::annotate("std::capability")]] {
    int fd;
} Dev;
void dev_write(Dev *dev, const char *text);

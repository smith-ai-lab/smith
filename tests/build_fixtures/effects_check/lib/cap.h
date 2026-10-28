#pragma once
typedef struct [[clang::annotate("std::capability")]] {
    int fd;
} Cap;
void cap_write(Cap *cap, const char *text);
void cap_leak(const char *text);
void cap_leak_deep(const char *text);
int cap_pure(int x);
Cap cap_open(void);

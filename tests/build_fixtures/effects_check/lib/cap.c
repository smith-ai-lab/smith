#include "cap.h"

#include <stdio.h>
#include <unistd.h>

static void helper(const char *text) {
    fputs(text, stdout);
}
static void middle(const char *text) {
    helper(text);
}
void cap_write(Cap *cap, const char *text) {
    (void)cap;
    middle(text);
}
void cap_leak(const char *text) {
    fputs(text, stdout);
}
void cap_leak_deep(const char *text) {
    middle(text);
}
int cap_pure(int x) {
    return x + 1;
}
Cap cap_open(void) {
    (void)write(1, "x", 1);
    return (Cap){1};
}

// In a C file the idioms are inferred and written to the summary, and no origin is an error.
#include <stdlib.h>
#include <string.h>

char *copy(const char *text) { return strdup(text); }
char *find(char *text, int letter) { return strchr(text, letter); }
const char *home(void) { return getenv("HOME"); }

char *unknown(char *(*next)(char *), char *text) {
    char *cursor = text;
    for (int i = 0; i < 3; i++)
        cursor = next(cursor);
    return cursor;
}

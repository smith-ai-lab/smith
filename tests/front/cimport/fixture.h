// The fixture of tests/front_cimport_test.smith: one of each thing front::cimport reads, and a union it cannot.
typedef unsigned long Id;
typedef int (*Compare)(const void *, const void *);

struct Point {
    int x;
    double y;
};
struct Opaque;
struct Holder {
    struct Opaque *inner;
    Compare compare;
    int (*callback)(void *context, int value);
    const char *name;
    Id id;
    float weights[4];
};
union Either {
    int whole;
    float part;
};
struct Flags {
    int ready : 1;
};

int add(int a, int b);
int log_line(const char *format, ...);
struct Point origin(void);
void sort(struct Holder *holder, Compare compare);
long double precise(void);

enum Color { Red, Green = 5, Below = -3 };
enum { Anon_a, Anon_b = 7 };
#define A 8192
#define B (A * 2u)
#define PI 3.14159
#define NAME "smith"
#define NEG (-1)
#define BIG 8192UL
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define NOT_CONST some_function()
#define EMPTY
#define KEYWORD_ONLY const
#define __INTERNAL 1
static const int Limit = 99;

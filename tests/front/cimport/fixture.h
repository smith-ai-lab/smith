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

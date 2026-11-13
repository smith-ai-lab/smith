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

// A slice as C passes it (#3327): these two are `[]char` and `[]mut char`, the third has a field more and stays a
// record.
struct smith_array_const_char_n {
    const char *data;
    unsigned long count[1];
};
struct smith_array_char_n {
    char *data;
    unsigned long count[1];
};
struct smith_array_odd_n {
    int *data;
    unsigned long count[1];
    int extra;
};
struct smith_array_const_u8_n {
    const unsigned char *data;
    Id count[1]; // a typedef of `unsigned long`, as the generated headers write `size_t`
};
struct Text {
    struct smith_array_const_char_n name;
    struct smith_array_char_n buffer;
    struct smith_array_const_u8_n bytes;
};
struct smith_array_const_char_n label(struct smith_array_const_char_n text, struct smith_array_odd_n odd);

void poison(void const volatile *region, unsigned long size); // a volatile pointee in a parameter is read as plain
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

typedef unsigned short Width; // no function or struct mentions it

// A transparent union parameter is its first member's type (glibc's connect takes one).
typedef union {
    const struct Point *point;
    const struct Opaque *opaque;
} PointArg __attribute__((__transparent_union__));
int use_point_arg(PointArg arg);

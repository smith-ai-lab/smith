// C++ functions with no identifier for a name (constructor, destructor, operator, conversion) are judged like any other.
#include <stdio.h>

struct Counter {
    int value;
    Counter() : value(0) {}
    explicit Counter(int start) : value(start) {}
    ~Counter() {}
    operator int() const { return value; }
    Counter operator+(const Counter &other) const { return Counter(value + other.value); }
};

struct Opens {
    Opens() { fopen("x", "r"); }
};

int cxx(void) {
    Counter a(1);
    Counter b;
    Opens o;
    return int(a + b);
}

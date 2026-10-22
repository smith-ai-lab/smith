// A trusted function sits at the program's edge: its smith_requires is proved at each call and its smith_ensures is
// assumed, and its body (inline assembly here) is not read. main has no contract and is not proved; a function with no
// contract is not proved either, but a proved function that calls it reads it.
#include <smith/contract.h>

#include <stdint.h>

// The edge: a sensor read through assembly, which cannot be proved.
int32_t sensor_percent(void) {
    smith_trusted();
    int32_t value;
    __asm__ volatile("" : "=r"(value));
    smith_ensures(0 <= value && value <= 100);
    return value;
}

// Proved from the contract of sensor_percent alone.
int32_t scaled(void) {
    int32_t percent = sensor_percent();
    int32_t doubled = percent * 2;
    smith_ensures(0 <= doubled && doubled <= 200);
    return doubled;
}

// What the contract does not give.
int32_t scaled_too_far(void) {
    int32_t percent = sensor_percent();
    int32_t tripled = percent * 3;
    smith_ensures(0 <= tripled && tripled <= 200);
    return tripled;
}

// No contract: not proved, and nothing is said of it, but a proved function that calls it reads its body.
int32_t helper(int32_t x) {
    return x + 1;
}

// A trusted function with no contract at all: it only marks the edge.
void announce(int32_t value) {
    smith_trusted();
    (void)value;
}

int32_t after_announce(int32_t x) {
    smith_requires(x >= 0 && x < 100);
    announce(x);
    int32_t y = helper(x);
    smith_ensures(y == x + 1);
    return y;
}

int main(void) {
    return scaled() + scaled_too_far() + after_announce(1);
}

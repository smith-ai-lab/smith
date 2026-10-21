// Inline assembly cannot be proved. The contract of sensor_percent is what lets scaled be proved without reading it.
#include <smith/contract.h>

#include <stdint.h>

int32_t sensor_percent(void) {
    int32_t value;
    __asm__ volatile("" : "=r"(value));
    smith_ensures(0 <= value && value <= 100);
    return value;
}

int32_t scaled(void) {
    int32_t percent = sensor_percent();
    int32_t doubled = percent * 2;
    smith_ensures(0 <= doubled && doubled <= 200);
    return doubled;
}

// What sensor_percent promises is not enough for this one.
int32_t scaled_too_far(void) {
    int32_t percent = sensor_percent();
    int32_t tripled = percent * 3;
    smith_ensures(0 <= tripled && tripled <= 200);
    return tripled;
}

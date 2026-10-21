// The issue's clamp, and a function that uses it through its contract alone.
#include <smith/contract.h>

#include <stdint.h>

int32_t clamp(int32_t x, int32_t low, int32_t high) {
    smith_requires(low <= high);
    int32_t result = x < low ? low : x > high ? high : x;
    smith_ensures(low <= result && result <= high);
    return result;
}

// How wide the filled part of a health bar is, in pixels: four to a point of health.
int32_t draw_health(int32_t health, int32_t max_health) {
    smith_requires(0 <= max_health && max_health <= 1000);
    int32_t points = clamp(health, 0, max_health);
    int32_t pixels = points * 4;
    smith_ensures(0 <= pixels && pixels <= 4 * max_health);
    return pixels;
}

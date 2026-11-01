#include <stdint.h>
#include <stdio.h>
typedef struct {
char name[16];
constexpr Type of;
} Field;
typedef char *Text;
constexpr Type count_type = __UINT32_TYPE__;
constexpr Type names[3] = {__INT32_TYPE__, double, Text};
constexpr Field fields[2] = {{"length", __SIZE_TYPE__}, {"ratio", float}};
typedef I32 Meters;
constexpr Type distance = Meters;
static_assert(count_type == uint32_t);
static_assert(is_record(fields[0].of) == 0);
static_assert(count_type == __UINT32_TYPE__);
static_assert(distance == __INT32_TYPE__);
static_assert(distance == int);
static_assert(names[0] != names[1]);
static_assert(fields[0].of == size_t);
static_assert(fields[1].of != fields[0].of);
static_assert(sizeof(names[2]) == 8 && alignof(names[1]) == 8);
static_assert(is_integer(count_type) && !is_signed(count_type) && is_signed(distance));
static_assert(is_float(names[1]) && !is_float(names[0]) && is_pointer(names[2]));
static bool small(Type t) {
return sizeof(t) <= 4;
}
static Type wider(Type a, Type b) {
return sizeof(a) >= sizeof(b) ? a : b;
}
static_assert(small(__INT32_TYPE__) && small(__UINT8_TYPE__) && !small(double) && !small(Text));
static_assert(wider(__INT32_TYPE__, double) == double);
static_assert(wider(__UINT8_TYPE__, __UINT16_TYPE__) == __UINT16_TYPE__);
static_assert(small(wider(__INT8_TYPE__, __UINT16_TYPE__)) && !small(wider(__INT32_TYPE__, __INT64_TYPE__)));
int main() {
constexpr Type local = wider(__UINT8_TYPE__, __INT64_TYPE__);
local big = 1;
big <<= 40;
count_type small_one = 7;
constexpr Type length_type = fields[0].of;
constexpr Type ratio_type = names[1];
length_type length = 12;
ratio_type ratio = 0.5;
printf("%lld %u %zu %g %zu\n", (long long)big, small_one, (size_t)length, ratio, sizeof(local));
return 0;
}

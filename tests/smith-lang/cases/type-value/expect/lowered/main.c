#include <stdint.h>
#include <stdio.h>
typedef struct {
char name[16];
constexpr Type of;
} Field;
typedef char *Text;
constexpr Type CountType = __UINT32_TYPE__;
constexpr Type names[3] = {__INT32_TYPE__, double, Text};
constexpr Field fields[2] = {{"length", __SIZE_TYPE__}, {"ratio", float}};
typedef I32 Meters;
constexpr Type Distance = Meters;
static_assert(CountType == uint32_t);
static_assert(Struct(fields[0].of) == 0);
static_assert(CountType == __UINT32_TYPE__);
static_assert(Distance == __INT32_TYPE__);
static_assert(Distance == int);
static_assert(names[0] != names[1]);
static_assert(fields[0].of == size_t);
static_assert(fields[1].of != fields[0].of);
static_assert(sizeof(names[2]) == 8 && alignof(names[1]) == 8);
static_assert(Int(CountType) && !Signed(CountType) && Signed(Distance));
static_assert(Float(names[1]) && !Float(names[0]) && Pointer(names[2]));
static bool small(Type t) {
return sizeof(t) <= 4;
}
static Type Wider(Type a, Type b) {
return sizeof(a) >= sizeof(b) ? a : b;
}
static_assert(small(__INT32_TYPE__) && small(__UINT8_TYPE__) && !small(double) && !small(Text));
static_assert(Wider(__INT32_TYPE__, double) == double);
static_assert(Wider(__UINT8_TYPE__, __UINT16_TYPE__) == __UINT16_TYPE__);
static_assert(small(Wider(__INT8_TYPE__, __UINT16_TYPE__)) && !small(Wider(__INT32_TYPE__, __INT64_TYPE__)));
int main() {
constexpr Type Local = Wider(__UINT8_TYPE__, __INT64_TYPE__);
Local big = 1;
big <<= 40;
CountType small_one = 7;
constexpr Type LengthType = fields[0].of;
constexpr Type RatioType = names[1];
LengthType length = 12;
RatioType ratio = 0.5;
printf("%lld %u %zu %g %zu\n", (long long)big, small_one, (size_t)length, ratio, sizeof(Local));
return 0;
}

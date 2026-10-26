#include <stdio.h>

static constexpr __INT32_TYPE__ scale_base = 10;
static constexpr __INT32_TYPE__ scale_slots = scale_base / 2;
typedef enum { scale_Plain, scale_Boosted } scale_Mode;
typedef struct {
__INT32_TYPE__ low;
__INT32_TYPE__ high;
} scale_Range;
struct scale_Counter {
__INT32_TYPE__ calls;
};
typedef struct scale_Counter scale_Counter;
static __INT32_TYPE__ scale_table[scale_slots] = {1, 2, 3, 4, 5};
static struct scale_Counter scale_counter;
static scale_Range scale_widen(scale_Range r, FILE *log);
static scale_Range scale_widen(scale_Range r, FILE *log) {
(void)log;
scale_counter.calls += 1;
return (scale_Range){r.low - scale_base, r.high + scale_base};
}
static __INT32_TYPE__ scale_unused(__INT32_TYPE__ x) {
return x + 1;
}
static inline __INT32_TYPE__ scale_clamp_to_base(__INT32_TYPE__ x) {
return x > scale_base ? scale_base : x;
}
__INT32_TYPE__ scale_calls() __asm__("_scale::calls");
__INT32_TYPE__ scale_calls() {
return scale_counter.calls + scale_unused(0) - 1;
} [ [ clang :: unavailable ( "scale::spread is a generic function: call it from a .smith module" ) ] ] void scale_spread ( void ) ;

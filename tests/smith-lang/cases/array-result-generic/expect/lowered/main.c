#include <stdio.h>
#include "split.h"

__INT32_TYPE__ main(void) {
split_Key key = {10, 20};
split_Key keys[3] =smith_call_failed;
split_Key pair[2] =smith_call_failed;
__INT32_TYPE__ sevens[4] =smith_call_failed;
float halves[2] =__extension__({struct smith_array_f32_2{float data[2];};extern struct smith_array_f32_2 split_both__f32(float a,float b)__asm__("_split::both(float)");split_both__f32(0.5f,1.5f);});
printf("%llu %llu %llu %llu\n", keys[0].high, keys[1].low, keys[2].low, pair[1].high);
printf("%d %d %d %.1f %.1f\n", sevens[0], sevens[3], (__INT32_TYPE__)countof(sevens), halves[0], halves[1]);
return 0;
}
struct smith_array_f32_2{float data[2];};extern struct smith_array_f32_2 split_both__f32(float a,float b)__asm__("_split::both(float)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_array_f32_2 split_both__f32(float a,float b){return(struct smith_array_f32_2){.data={a,b}};}

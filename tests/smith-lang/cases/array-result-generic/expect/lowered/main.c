#include <stdio.h>
#include "split.h"

__INT32_TYPE__ main(void) {
split_Key key = {10, 20};auto smith_w_keys =smith_call_failed; static_assert (sizeof smith_w_keys.data / sizeof smith_w_keys.data [0] == 3, "the array has another size than the call's result");
split_Key keys[3]= {smith_w_keys.data [0], smith_w_keys.data [1], smith_w_keys.data [2]};auto smith_w_pair =smith_call_failed; static_assert (sizeof smith_w_pair.data / sizeof smith_w_pair.data [0] == 2, "the array has another size than the call's result");
split_Key pair[2]= {smith_w_pair.data [0], smith_w_pair.data [1]};auto smith_w_sevens =smith_call_failed; static_assert (sizeof smith_w_sevens.data / sizeof smith_w_sevens.data [0] == 4, "the array has another size than the call's result");
__INT32_TYPE__ sevens[4]= {smith_w_sevens.data [0], smith_w_sevens.data [1], smith_w_sevens.data [2], smith_w_sevens.data [3]};auto smith_w_halves =__extension__({struct smith_array_f32_2{float data[2];};extern struct smith_array_f32_2 split_both__f32(float a,float b)__asm__("_split::both(float)");split_both__f32(0.5f,1.5f);}); static_assert (sizeof smith_w_halves.data / sizeof smith_w_halves.data [0] == 2, "the array has another size than the call's result");
float halves[2]= {smith_w_halves.data [0], smith_w_halves.data [1]};
printf("%llu %llu %llu %llu\n", keys[0].high, keys[1].low, keys[2].low, pair[1].high);
printf("%d %d %d %.1f %.1f\n", sevens[0], sevens[3], (__INT32_TYPE__)countof(sevens), halves[0], halves[1]);
return 0;
}
struct smith_array_f32_2{float data[2];};extern struct smith_array_f32_2 split_both__f32(float a,float b)__asm__("_split::both(float)");
[[gnu::weak]][[gnu::visibility("hidden")]]struct smith_array_f32_2 split_both__f32(float a,float b){return(struct smith_array_f32_2){.data={a,b}};}

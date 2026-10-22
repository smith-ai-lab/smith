#include "mix/modes.h"

__INT32_TYPE__ mix_use_both(void) __asm__("_mix::use::both");
__INT32_TYPE__ mix_use_both(void) {
return mix_modes_score(MixMode_Fast, mix_modes_Level_Low) + mix_modes_score(MixMode_Slow, mix_modes_Level_High);
}

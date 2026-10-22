#include <stdio.h>

#include "mix/modes.h"
#include "mix/use.h"

int main(void) {
    printf("%d %d %d\n", (int)mix_modes_score(MixMode_Slow, mix_modes_Level_High), (int)MixMode_Fast,
           (int)mix_use_both());
    return 0;
}

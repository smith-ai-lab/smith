#include "scale.h"

scale_Key mid_make() __asm__("_mid::make");
scale_Key mid_make() {
return (scale_Key){7};
}

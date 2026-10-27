#include "lib/gen.h"

typedef struct {
__SIZE_TYPE__ size;
} ShapeBox;
__SIZE_TYPE__ lib_shape_doubled(ShapeBox *box, __SIZE_TYPE__ count) __asm__("_lib::shape::doubled");
__SIZE_TYPE__ lib_shape_doubled(ShapeBox *box, __SIZE_TYPE__ count) {
return smith_call_failed + box->size;
}

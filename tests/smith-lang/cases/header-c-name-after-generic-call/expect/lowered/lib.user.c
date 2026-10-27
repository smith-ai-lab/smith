#include "lib/shape.h"

__SIZE_TYPE__ lib_user_size_of(ShapeBox *box) __asm__("_lib::user::size_of");
__SIZE_TYPE__ lib_user_size_of(ShapeBox *box) {
return box->size;
}

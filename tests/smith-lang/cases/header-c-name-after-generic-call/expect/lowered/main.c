#include "lib/shape.h"
#include "lib/user.h"
#include <stdio.h>
int main() {
ShapeBox box = {.size = 4};
printf("%zu %zu\n", lib_user_size_of(&box), lib_shape_doubled(&box, 3));
return 0;
}

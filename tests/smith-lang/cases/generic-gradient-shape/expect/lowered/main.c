#include "shape.h"
#include <stdio.h>

static double main_loss(const shape_Params *p, const shape_Batch *batch) {
return (p->a + p->b) * batch->scale;
}
static double main_plain(const shape_Params *p) {
return p->a * 2;
}
int main(void) {
shape_Params p = {1.0, 2.0};
shape_Batch batch = {3.0};
shape_Params g =smith_call_failed;
shape_Params h =smith_call_failed;
printf("%.1f %.1f\n", g.a, h.a);
return 0;
}

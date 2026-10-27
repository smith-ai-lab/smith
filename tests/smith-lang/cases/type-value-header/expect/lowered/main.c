#include <stdio.h>
#include "geo/units.h"

extern __INT32_TYPE__ user(void);
__INT32_TYPE__ main(void) {
geo_units_Meters m = geo_units_add(1.5, 2.5);
geo_units_Handler h = geo_units_handler();
geo_units_Block b = {1, 2};
geo_units_Request r = {7};
geo_units_Pointers p = {&r};
geo_feet f = 0.5f;
printf("%g %d %d %d %g\n", m, h(&r), b[1], p[0]->id, (double)f);
return user();
}

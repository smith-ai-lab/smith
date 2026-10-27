#include <stdio.h>

#include "geo/units.h"

// C names the aliases with the typedefs the header carries.
int user(void) {
    geo_units_Meters m = geo_units_add(5, 6);
    geo_units_Handler h = geo_units_handler();
    geo_units_Block b = {3, 4};
    struct geo_units_Request r = {9};
    geo_units_Pointers p = {&r, NULL};
    geo_feet f = 2;
    printf("%g %d %d %d %g\n", m, h(&r), b[1], p[0]->id, (double)f);
    return 0;
}

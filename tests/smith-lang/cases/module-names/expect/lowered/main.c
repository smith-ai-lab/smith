#include "names/fields.h"
#include <stdio.h>
int main() {
names_fields_Stats s = names_fields_make(2, 3);
printf("%d %d\n", s.count, names_fields_combine(&s));
return 0;
}
